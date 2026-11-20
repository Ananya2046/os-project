/**
 * gui_main.c - Native GTK 3 & VTE Desktop Frontend for Mini Linux Shell (mysh)
 * 
 * OS Concepts Demonstrated:
 * 1. Pseudo-Terminals (PTY): Allocating master/slave tty pairs for terminal control.
 * 2. Inter-Process Communication (IPC): Named pipes (FIFOs) for asynchronous event streaming.
 * 3. Kernel Process Inspection: Reading /proc filesystem for live process hierarchy.
 * 4. Signal Management: Sending real POSIX signals (SIGTSTP, SIGCONT, SIGKILL) to PGIDs.
 * 
 * Course: OS Lab (24B11CS312), JIIT Noida
 * Team Members: Tanishq Pundir, Anvi, Annanya Aggarwal, Palak Chauhan
 */

#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <gtk/gtk.h>
#ifndef NO_VTE
#include <vte/vte.h>
#else
#include <pty.h>
#include <utmp.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <errno.h>
#include <dirent.h>

#define MAX_JOBS 64
#define MAX_STAGES 8

typedef struct {
    int job_id;
    pid_t pgid;
    char command[256];
    char state[32];
    int active;
} GuiJob;

typedef struct {
    char name[64];
    pid_t pid;
} PipelineStage;

typedef struct {
    GtkWidget *window;
#ifndef NO_VTE
    GtkWidget *vte_terminal;
#else
    GtkWidget *text_view;
    GtkTextBuffer *text_buffer;
    int pty_master;
#endif
    GtkListStore *jobs_store;
    GtkTreeView *jobs_tree;
    GtkListStore *proc_store;
    GtkTreeView *proc_tree;
    GtkWidget *pipeline_drawing_area;
    GtkTextBuffer *signal_log_buffer;
    GtkTextView *signal_log_view;
    GtkWidget *status_label;
    GtkCssProvider *css_provider;
    
    char fifo_path[256];
    int fifo_fd;
    pid_t shell_pid;
    
    GuiJob jobs[MAX_JOBS];
    int job_count;
    
    PipelineStage stages[MAX_STAGES];
    int stage_count;
    
    char cwd[512];
    int last_exit_status;
    gboolean is_dark_theme;
} GuiApp;

static GuiApp app;

/* Enum for Jobs ListStore */
enum {
    JOB_COL_ID,
    JOB_COL_PGID,
    JOB_COL_CMD,
    JOB_COL_STATE,
    JOB_COL_COLOR,
    JOB_NUM_COLS
};

/* Enum for Proc ListStore */
enum {
    PROC_COL_PID,
    PROC_COL_PPID,
    PROC_COL_PGID,
    PROC_COL_STATE,
    PROC_COL_CMD,
    PROC_NUM_COLS
};

static void log_signal_event(const char *event_text) {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char time_str[32];
    strftime(time_str, sizeof(time_str), "%H:%M:%S", tm_info);

    char full_msg[512];
    snprintf(full_msg, sizeof(full_msg), "[%s] %s\n", time_str, event_text);

    GtkTextIter end;
    gtk_text_buffer_get_end_iter(app.signal_log_buffer, &end);
    gtk_text_buffer_insert(app.signal_log_buffer, &end, full_msg, -1);

    /* Auto scroll to bottom */
    GtkTextMark *mark = gtk_text_buffer_create_mark(app.signal_log_buffer, NULL, &end, FALSE);
    gtk_text_view_scroll_to_mark(app.signal_log_view, mark, 0.0, FALSE, 0.0, 1.0);
}

static void update_status_bar(void) {
    int running = 0, stopped = 0;
    for (int i = 0; i < app.job_count; i++) {
        if (app.jobs[i].active) {
            if (strcmp(app.jobs[i].state, "STOPPED") == 0) stopped++;
            else running++;
        }
    }

    char status_text[512];
    snprintf(status_text, sizeof(status_text),
             " Directory: %s  |  Running Jobs: %d  |  Stopped Jobs: %d  |  Last Exit: %d",
             app.cwd[0] ? app.cwd : "~", running, stopped, app.last_exit_status);
    gtk_label_set_text(GTK_LABEL(app.status_label), status_text);
}

static void refresh_jobs_view(void) {
    gtk_list_store_clear(app.jobs_store);
    for (int i = 0; i < app.job_count; i++) {
        if (app.jobs[i].active) {
            GtkTreeIter iter;
            gtk_list_store_append(app.jobs_store, &iter);

            const char *color = "#10b981"; /* Green for running */
            if (strcmp(app.jobs[i].state, "STOPPED") == 0) {
                color = "#f59e0b"; /* Amber for stopped */
            } else if (strcmp(app.jobs[i].state, "DONE") == 0) {
                color = "#6b7280"; /* Grey for done */
            }

            gtk_list_store_set(app.jobs_store, &iter,
                               JOB_COL_ID, app.jobs[i].job_id,
                               JOB_COL_PGID, app.jobs[i].pgid,
                               JOB_COL_CMD, app.jobs[i].command,
                               JOB_COL_STATE, app.jobs[i].state,
                               JOB_COL_COLOR, color,
                               -1);
        }
    }
    update_status_bar();
}

static gboolean on_draw_pipeline(GtkWidget *widget, cairo_t *cr, gpointer user_data) {
    (void)widget;
    (void)user_data;
    int width = gtk_widget_get_allocated_width(widget);
    int height = gtk_widget_get_allocated_height(widget);

    /* Background fill */
    cairo_set_source_rgb(cr, 0.08, 0.10, 0.14);
    cairo_paint(cr);

    if (app.stage_count == 0) {
        cairo_set_source_rgb(cr, 0.4, 0.45, 0.5);
        cairo_select_font_face(cr, "Monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 11);
        cairo_move_to(cr, 15, height / 2 + 4);
        cairo_show_text(cr, "No active pipeline running (run e.g. ls | grep txt | wc -l)");
        return FALSE;
    }

    int box_w = 90;
    int box_h = 40;
    int spacing = 45;
    int total_w = app.stage_count * box_w + (app.stage_count - 1) * spacing;
    int start_x = (width - total_w) / 2;
    if (start_x < 10) start_x = 10;
    int y = (height - box_h) / 2;

    for (int i = 0; i < app.stage_count; i++) {
        int x = start_x + i * (box_w + spacing);

        /* Stage box */
        cairo_set_source_rgb(cr, 0.12, 0.16, 0.23);
        cairo_rectangle(cr, x, y, box_w, box_h);
        cairo_fill_preserve(cr);

        /* Stage border */
        cairo_set_source_rgb(cr, 0.06, 0.72, 0.50); /* Emerald border */
        cairo_set_line_width(cr, 2);
        cairo_stroke(cr);

        /* Command text */
        cairo_set_source_rgb(cr, 0.9, 0.95, 1.0);
        cairo_select_font_face(cr, "Monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, 10);
        cairo_move_to(cr, x + 8, y + 16);
        cairo_show_text(cr, app.stages[i].name);

        /* PID text */
        char pid_str[32];
        snprintf(pid_str, sizeof(pid_str), "PID: %d", (int)app.stages[i].pid);
        cairo_set_source_rgb(cr, 0.6, 0.65, 0.75);
        cairo_set_font_size(cr, 9);
        cairo_move_to(cr, x + 8, y + 32);
        cairo_show_text(cr, pid_str);

        /* Arrow to next stage */
        if (i < app.stage_count - 1) {
            int arrow_start = x + box_w;
            int arrow_end = arrow_start + spacing;
            int arrow_y = y + box_h / 2;

            cairo_set_source_rgb(cr, 0.4, 0.5, 0.65);
            cairo_set_line_width(cr, 2);
            cairo_move_to(cr, arrow_start + 4, arrow_y);
            cairo_line_to(cr, arrow_end - 4, arrow_y);
            cairo_stroke(cr);

            /* Arrow head */
            cairo_move_to(cr, arrow_end - 4, arrow_y);
            cairo_line_to(cr, arrow_end - 10, arrow_y - 4);
            cairo_line_to(cr, arrow_end - 10, arrow_y + 4);
            cairo_close_path(cr);
            cairo_fill(cr);

            /* Pipe label */
            cairo_set_source_rgb(cr, 0.8, 0.6, 0.2);
            cairo_set_font_size(cr, 8);
            cairo_move_to(cr, arrow_start + 12, arrow_y - 6);
            cairo_show_text(cr, "pipe");
        }
    }

    return FALSE;
}

static void update_process_panel(void) {
    gtk_list_store_clear(app.proc_store);

    DIR *proc = opendir("/proc");
    if (!proc) {
        /* Fallback for macOS / systems without procfs: show tracked stages */
        for (int i = 0; i < app.stage_count; i++) {
            GtkTreeIter iter;
            gtk_list_store_append(app.proc_store, &iter);
            gtk_list_store_set(app.proc_store, &iter,
                               PROC_COL_PID, (int)app.stages[i].pid,
                               PROC_COL_PPID, (int)app.shell_pid,
                               PROC_COL_PGID, (int)app.stages[i].pid,
                               PROC_COL_STATE, "R",
                               PROC_COL_CMD, app.stages[i].name,
                               -1);
        }
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(proc)) != NULL) {
        if (entry->d_name[0] < '0' || entry->d_name[0] > '9') continue;
        int pid = atoi(entry->d_name);

        char stat_path[256];
        snprintf(stat_path, sizeof(stat_path), "/proc/%d/stat", pid);
        FILE *f = fopen(stat_path, "r");
        if (!f) continue;

        int ppid, pgid;
        char comm[256], state;
        if (fscanf(f, "%*d (%255[^)]) %c %d %d", comm, &state, &ppid, &pgid) == 4) {
            /* Only show processes that are children or in the shell's process group */
            if (ppid == app.shell_pid || pgid == app.shell_pid || pid == app.shell_pid) {
                GtkTreeIter iter;
                gtk_list_store_append(app.proc_store, &iter);
                char state_str[2] = { state, '\0' };
                gtk_list_store_set(app.proc_store, &iter,
                                   PROC_COL_PID, pid,
                                   PROC_COL_PPID, ppid,
                                   PROC_COL_PGID, pgid,
                                   PROC_COL_STATE, state_str,
                                   PROC_COL_CMD, comm,
                                   -1);
            }
        }
        fclose(f);
    }
    closedir(proc);
}

static gboolean on_proc_timer(gpointer user_data) {
    (void)user_data;
    update_process_panel();
    return TRUE;
}

static void send_to_terminal(const char *text) {
#ifndef NO_VTE
    vte_terminal_feed_child(VTE_TERMINAL(app.vte_terminal), text, strlen(text));
#else
    if (app.pty_master >= 0) {
        write(app.pty_master, text, strlen(text));
    }
#endif
}

static void parse_fifo_event(const char *line) {
    char buf[1024];
    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *type = strtok(buf, "|\n");
    if (!type) return;

    if (strcmp(type, "JOB_ADD") == 0) {
        char *id_str = strtok(NULL, "|");
        char *pgid_str = strtok(NULL, "|");
        char *cmd = strtok(NULL, "|");
        char *state = strtok(NULL, "|\n");

        if (id_str && pgid_str && cmd) {
            int id = atoi(id_str);
            pid_t pgid = (pid_t)atoi(pgid_str);
            int idx = -1;
            for (int i = 0; i < app.job_count; i++) {
                if (app.jobs[i].job_id == id) { idx = i; break; }
            }
            if (idx == -1 && app.job_count < MAX_JOBS) {
                idx = app.job_count++;
            }
            if (idx >= 0) {
                app.jobs[idx].job_id = id;
                app.jobs[idx].pgid = pgid;
                strncpy(app.jobs[idx].command, cmd, sizeof(app.jobs[idx].command) - 1);
                strncpy(app.jobs[idx].state, state ? state : "RUNNING", sizeof(app.jobs[idx].state) - 1);
                app.jobs[idx].active = 1;
            }
            refresh_jobs_view();
            char msg[512];
            snprintf(msg, sizeof(msg), "Job [%d] added (PGID: %d): %s", id, (int)pgid, cmd);
            log_signal_event(msg);
        }
    } else if (strcmp(type, "JOB_STATE") == 0) {
        char *id_str = strtok(NULL, "|");
        char *state = strtok(NULL, "|\n");
        if (id_str && state) {
            int id = atoi(id_str);
            for (int i = 0; i < app.job_count; i++) {
                if (app.jobs[i].job_id == id && app.jobs[i].active) {
                    strncpy(app.jobs[i].state, state, sizeof(app.jobs[i].state) - 1);
                    if (strcmp(state, "DONE") == 0) {
                        app.jobs[i].active = 0;
                    }
                    break;
                }
            }
            refresh_jobs_view();
            char msg[256];
            snprintf(msg, sizeof(msg), "Job [%d] state changed to: %s", id, state);
            log_signal_event(msg);
        }
    } else if (strcmp(type, "JOB_DONE") == 0) {
        char *id_str = strtok(NULL, "|");
        char *code_str = strtok(NULL, "|\n");
        if (id_str) {
            int id = atoi(id_str);
            for (int i = 0; i < app.job_count; i++) {
                if (app.jobs[i].job_id == id) {
                    app.jobs[i].active = 0;
                    break;
                }
            }
            refresh_jobs_view();
            char msg[256];
            snprintf(msg, sizeof(msg), "Job [%d] completed (exit code: %s)", id, code_str ? code_str : "0");
            log_signal_event(msg);
        }
    } else if (strcmp(type, "PIPE_CREATE") == 0) {
        char *stages_str = strtok(NULL, "|\n");
        if (stages_str) {
            int n = atoi(stages_str);
            app.stage_count = n > MAX_STAGES ? MAX_STAGES : n;
            for (int i = 0; i < app.stage_count; i++) {
                snprintf(app.stages[i].name, sizeof(app.stages[i].name), "Stage %d", i + 1);
                app.stages[i].pid = 0;
            }
            gtk_widget_queue_draw(app.pipeline_drawing_area);
            char msg[256];
            snprintf(msg, sizeof(msg), "Created IPC pipeline with %d stages", n);
            log_signal_event(msg);
        }
    } else if (strcmp(type, "PROC_NEW") == 0) {
        char *pid_str = strtok(NULL, "|");
        char *pgid_str = strtok(NULL, "|");
        char *cmd = strtok(NULL, "|\n");
        if (pid_str && cmd) {
            pid_t pid = (pid_t)atoi(pid_str);
            pid_t pgid = pgid_str ? (pid_t)atoi(pgid_str) : pid;
            for (int i = 0; i < app.stage_count; i++) {
                if (app.stages[i].pid == 0) {
                    app.stages[i].pid = pid;
                    strncpy(app.stages[i].name, cmd, sizeof(app.stages[i].name) - 1);
                    break;
                }
            }
            gtk_widget_queue_draw(app.pipeline_drawing_area);
            update_process_panel();
            char msg[256];
            snprintf(msg, sizeof(msg), "Forked child PID %d (PGID: %d): %s", (int)pid, (int)pgid, cmd);
            log_signal_event(msg);
        }
    } else if (strcmp(type, "SIG_EVENT") == 0) {
        char *sig_name = strtok(NULL, "|");
        char *details = strtok(NULL, "|\n");
        char msg[512];
        snprintf(msg, sizeof(msg), "%s -> %s", sig_name ? sig_name : "SIGNAL", details ? details : "");
        log_signal_event(msg);
    } else if (strcmp(type, "CWD_CHANGE") == 0) {
        char *new_cwd = strtok(NULL, "|\n");
        if (new_cwd) {
            strncpy(app.cwd, new_cwd, sizeof(app.cwd) - 1);
            update_status_bar();
        }
    }
}

static gboolean on_fifo_readable(GIOChannel *source, GIOCondition cond, gpointer user_data) {
    (void)cond;
    (void)user_data;
    char buffer[1024];
    gsize bytes_read = 0;
    GError *error = NULL;

    while (g_io_channel_read_line(source, buffer, sizeof(buffer), &bytes_read, NULL, &error) == G_IO_STATUS_NORMAL) {
        if (bytes_read > 0) {
            buffer[bytes_read] = '\0';
            parse_fifo_event(buffer);
        }
    }
    return TRUE;
}

/* Job control button callbacks */
static GuiJob *get_selected_job(void) {
    GtkTreeSelection *selection = gtk_tree_view_get_selection(app.jobs_tree);
    GtkTreeModel *model;
    GtkTreeIter iter;

    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        int job_id;
        gtk_tree_model_get(model, &iter, JOB_COL_ID, &job_id, -1);
        for (int i = 0; i < app.job_count; i++) {
            if (app.jobs[i].job_id == job_id && app.jobs[i].active) {
                return &app.jobs[i];
            }
        }
    }
    return NULL;
}

static void on_btn_stop(GtkWidget *btn, gpointer user_data) {
    (void)btn; (void)user_data;
    GuiJob *job = get_selected_job();
    if (job && job->pgid > 0) {
        kill(-job->pgid, SIGTSTP);
        char msg[256];
        snprintf(msg, sizeof(msg), "Sent SIGTSTP to Process Group -%d", (int)job->pgid);
        log_signal_event(msg);
    }
}

static void on_btn_bg(GtkWidget *btn, gpointer user_data) {
    (void)btn; (void)user_data;
    GuiJob *job = get_selected_job();
    if (job && job->pgid > 0) {
        char cmd[64];
        snprintf(cmd, sizeof(cmd), "bg %%%d\n", job->job_id);
        send_to_terminal(cmd);
        kill(-job->pgid, SIGCONT);
        char msg[256];
        snprintf(msg, sizeof(msg), "Sent SIGCONT (bg) to Process Group -%d", (int)job->pgid);
        log_signal_event(msg);
    }
}

static void on_btn_fg(GtkWidget *btn, gpointer user_data) {
    (void)btn; (void)user_data;
    GuiJob *job = get_selected_job();
    if (job) {
        char cmd[64];
        snprintf(cmd, sizeof(cmd), "fg %%%d\n", job->job_id);
        send_to_terminal(cmd);
    }
}

static void on_btn_kill(GtkWidget *btn, gpointer user_data) {
    (void)btn; (void)user_data;
    GuiJob *job = get_selected_job();
    if (job && job->pgid > 0) {
        kill(-job->pgid, SIGKILL);
        char msg[256];
        snprintf(msg, sizeof(msg), "Sent SIGKILL to Process Group -%d", (int)job->pgid);
        log_signal_event(msg);
    }
}

/* Demo Sequence Runner */
typedef struct {
    int step;
} DemoState;

static gboolean on_demo_tick(gpointer data) {
    DemoState *state = (DemoState *)data;
    state->step++;

    switch (state->step) {
        case 1:
            send_to_terminal("echo \"=== DEMO: I/O Redirection ===\"\n");
            break;
        case 2:
            send_to_terminal("echo \"hello from mysh\" > demo.txt ; cat < demo.txt\n");
            break;
        case 3:
            send_to_terminal("echo \"=== DEMO: Multi-Stage IPC Pipeline ===\"\n");
            break;
        case 4:
            send_to_terminal("ls -l src | grep \".c\" | wc -l\n");
            break;
        case 5:
            send_to_terminal("echo \"=== DEMO: Background Job Execution ===\"\n");
            break;
        case 6:
            send_to_terminal("sleep 15 &\n");
            break;
        case 7:
            send_to_terminal("jobs\n");
            break;
        case 8:
            send_to_terminal("echo \"=== Demo Sequence Finished ===\"\n");
            free(state);
            return FALSE;
    }
    return TRUE;
}

static void on_btn_demo(GtkWidget *btn, gpointer user_data) {
    (void)btn; (void)user_data;
    DemoState *state = malloc(sizeof(DemoState));
    state->step = 0;
    g_timeout_add(1200, on_demo_tick, state);
}

static void on_btn_about(GtkWidget *btn, gpointer user_data) {
    (void)btn; (void)user_data;
    GtkWidget *dialog = gtk_about_dialog_new();
    gtk_about_dialog_set_program_name(GTK_ABOUT_DIALOG(dialog), "Mini Linux Shell (mysh)");
    gtk_about_dialog_set_version(GTK_ABOUT_DIALOG(dialog), "v2.0 Native GUI");
    gtk_about_dialog_set_comments(GTK_ABOUT_DIALOG(dialog),
        "Operating Systems Lab (24B11CS312) - JIIT Noida\n"
        "A complete Linux shell with Process and Job Control,\n"
        "IPC Pipelines, File Redirection, and POSIX Signal Handling.\n\n"
        "Team Members:\n"
        "• Tanishq Pundir\n"
        "• Anvi\n"
        "• Annanya Aggarwal\n"
        "• Palak Chauhan");
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

static void on_quick_cmd(GtkWidget *btn, gpointer user_data) {
    (void)btn;
    const char *cmd = (const char *)user_data;
    if (cmd) {
        send_to_terminal(cmd);
        send_to_terminal("\n");
    }
}

static void on_window_destroy(GtkWidget *widget, gpointer user_data) {
    (void)widget; (void)user_data;
    /* Clean up FIFO and kill child processes */
    if (app.fifo_fd >= 0) {
        close(app.fifo_fd);
    }
    if (app.fifo_path[0]) {
        unlink(app.fifo_path);
    }
    if (app.shell_pid > 0) {
        kill(-app.shell_pid, SIGHUP);
        kill(-app.shell_pid, SIGCONT);
    }
    gtk_main_quit();
}

int main(int argc, char **argv) {
    gtk_init(&argc, &argv);

    memset(&app, 0, sizeof(app));
    app.last_exit_status = 0;
    app.is_dark_theme = TRUE;

    /* Create non-blocking FIFO for IPC */
    snprintf(app.fifo_path, sizeof(app.fifo_path), "/tmp/mysh_gui_fifo_%d", getpid());
    mkfifo(app.fifo_path, 0600);
    app.fifo_fd = open(app.fifo_path, O_RDWR | O_NONBLOCK);

    /* Main Window */
    app.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(app.window), "Mini Linux Shell - Process & Job Control");
    gtk_window_set_default_size(GTK_WINDOW(app.window), 1080, 680);
    g_signal_connect(app.window, "destroy", G_CALLBACK(on_window_destroy), NULL);

    /* Main vertical container */
    GtkWidget *vbox_main = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(app.window), vbox_main);

    /* 1. Header Toolbar */
    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(toolbar), 6);
    gtk_box_pack_start(GTK_BOX(vbox_main), toolbar, FALSE, FALSE, 0);

    GtkWidget *btn_demo = gtk_button_new_with_label("▶ Run Demo");
    g_signal_connect(btn_demo, "clicked", G_CALLBACK(on_btn_demo), NULL);
    gtk_box_pack_start(GTK_BOX(toolbar), btn_demo, FALSE, FALSE, 0);

    GtkWidget *btn_pipe = gtk_button_new_with_label("🔗 Pipeline Demo");
    g_signal_connect(btn_pipe, "clicked", G_CALLBACK(on_quick_cmd), "cat src/*.c | grep include | wc -l");
    gtk_box_pack_start(GTK_BOX(toolbar), btn_pipe, FALSE, FALSE, 0);

    GtkWidget *btn_bgjob = gtk_button_new_with_label("⏳ sleep 20 &");
    g_signal_connect(btn_bgjob, "clicked", G_CALLBACK(on_quick_cmd), "sleep 20 &");
    gtk_box_pack_start(GTK_BOX(toolbar), btn_bgjob, FALSE, FALSE, 0);

    GtkWidget *btn_pwd = gtk_button_new_with_label("📁 pwd");
    g_signal_connect(btn_pwd, "clicked", G_CALLBACK(on_quick_cmd), "pwd");
    gtk_box_pack_start(GTK_BOX(toolbar), btn_pwd, FALSE, FALSE, 0);

    GtkWidget *btn_about = gtk_button_new_with_label("ℹ️ About Team");
    g_signal_connect(btn_about, "clicked", G_CALLBACK(on_btn_about), NULL);
    gtk_box_pack_end(GTK_BOX(toolbar), btn_about, FALSE, FALSE, 0);

    /* 2. Main Horizontal Paned Area */
    GtkWidget *hpaned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_set_position(GTK_PANED(hpaned), 580);
    gtk_box_pack_start(GTK_BOX(vbox_main), hpaned, TRUE, TRUE, 0);

    /* 2a. Left Pane: Terminal Widget */
    GtkWidget *left_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_paned_pack1(GTK_PANED(hpaned), left_box, TRUE, FALSE);

#ifndef NO_VTE
    app.vte_terminal = vte_terminal_new();
    gtk_box_pack_start(GTK_BOX(left_box), app.vte_terminal, TRUE, TRUE, 0);

    /* Spawn ./mysh with --gui-fifo */
    char *spawn_argv[] = { "./mysh", "--gui-fifo", app.fifo_path, NULL };
    GError *spawn_err = NULL;
    vte_terminal_spawn_async(
        VTE_TERMINAL(app.vte_terminal),
        VTE_PTY_DEFAULT,
        NULL,
        spawn_argv,
        NULL,
        G_SPAWN_DO_NOT_REAP_CHILD,
        NULL, NULL, NULL, -1, NULL, NULL, &spawn_err
    );
#else
    /* Fallback using GtkTextView */
    app.text_view = gtk_text_view_new();
    app.text_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(app.text_view));
    GtkWidget *scrolled_term = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scrolled_term), app.text_view);
    gtk_box_pack_start(GTK_BOX(left_box), scrolled_term, TRUE, TRUE, 0);
#endif

    /* 2b. Right Pane: Jobs, Pipeline Visualizer, Processes, Signals */
    GtkWidget *right_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER(right_box), 6);
    gtk_paned_pack2(GTK_PANED(hpaned), right_box, TRUE, FALSE);

    /* Job Table Header */
    GtkWidget *lbl_jobs = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(lbl_jobs), "<b>Active Jobs (Job Control)</b>");
    gtk_box_pack_start(GTK_BOX(right_box), lbl_jobs, FALSE, FALSE, 2);

    /* Jobs TreeView */
    app.jobs_store = gtk_list_store_new(JOB_NUM_COLS, G_TYPE_INT, G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    app.jobs_tree = GTK_TREE_VIEW(gtk_tree_view_new_with_model(GTK_TREE_MODEL(app.jobs_store)));

    GtkCellRenderer *r_txt = gtk_cell_renderer_text_new();
    gtk_tree_view_insert_column_with_attributes(app.jobs_tree, -1, "ID", r_txt, "text", JOB_COL_ID, NULL);
    gtk_tree_view_insert_column_with_attributes(app.jobs_tree, -1, "PGID", r_txt, "text", JOB_COL_PGID, NULL);
    gtk_tree_view_insert_column_with_attributes(app.jobs_tree, -1, "Command", r_txt, "text", JOB_COL_CMD, NULL);
    gtk_tree_view_insert_column_with_attributes(app.jobs_tree, -1, "State", r_txt, "text", JOB_COL_STATE, "foreground", JOB_COL_COLOR, NULL);

    GtkWidget *scroll_jobs = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_size_request(scroll_jobs, -1, 100);
    gtk_container_add(GTK_CONTAINER(scroll_jobs), GTK_WIDGET(app.jobs_tree));
    gtk_box_pack_start(GTK_BOX(right_box), scroll_jobs, FALSE, FALSE, 0);

    /* Job Control Buttons */
    GtkWidget *box_job_btns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_pack_start(GTK_BOX(right_box), box_job_btns, FALSE, FALSE, 2);

    GtkWidget *btn_stop = gtk_button_new_with_label("⏸ Stop (Ctrl+Z)");
    g_signal_connect(btn_stop, "clicked", G_CALLBACK(on_btn_stop), NULL);
    gtk_box_pack_start(GTK_BOX(box_job_btns), btn_stop, TRUE, TRUE, 0);

    GtkWidget *btn_bg = gtk_button_new_with_label("▶ bg");
    g_signal_connect(btn_bg, "clicked", G_CALLBACK(on_btn_bg), NULL);
    gtk_box_pack_start(GTK_BOX(box_job_btns), btn_bg, TRUE, TRUE, 0);

    GtkWidget *btn_fg = gtk_button_new_with_label("⬆ fg");
    g_signal_connect(btn_fg, "clicked", G_CALLBACK(on_btn_fg), NULL);
    gtk_box_pack_start(GTK_BOX(box_job_btns), btn_fg, TRUE, TRUE, 0);

    GtkWidget *btn_kill = gtk_button_new_with_label("✖ Kill");
    g_signal_connect(btn_kill, "clicked", G_CALLBACK(on_btn_kill), NULL);
    gtk_box_pack_start(GTK_BOX(box_job_btns), btn_kill, TRUE, TRUE, 0);

    /* Pipeline Visualizer (Cairo Drawing Area) */
    GtkWidget *lbl_pipe = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(lbl_pipe), "<b>IPC Pipeline Visualizer</b>");
    gtk_box_pack_start(GTK_BOX(right_box), lbl_pipe, FALSE, FALSE, 2);

    app.pipeline_drawing_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(app.pipeline_drawing_area, -1, 60);
    g_signal_connect(app.pipeline_drawing_area, "draw", G_CALLBACK(on_draw_pipeline), NULL);
    gtk_box_pack_start(GTK_BOX(right_box), app.pipeline_drawing_area, FALSE, FALSE, 2);

    /* Process Table */
    GtkWidget *lbl_proc = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(lbl_proc), "<b>Process Hierarchy (/proc)</b>");
    gtk_box_pack_start(GTK_BOX(right_box), lbl_proc, FALSE, FALSE, 2);

    app.proc_store = gtk_list_store_new(PROC_NUM_COLS, G_TYPE_INT, G_TYPE_INT, G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING);
    app.proc_tree = GTK_TREE_VIEW(gtk_tree_view_new_with_model(GTK_TREE_MODEL(app.proc_store)));

    gtk_tree_view_insert_column_with_attributes(app.proc_tree, -1, "PID", r_txt, "text", PROC_COL_PID, NULL);
    gtk_tree_view_insert_column_with_attributes(app.proc_tree, -1, "PPID", r_txt, "text", PROC_COL_PPID, NULL);
    gtk_tree_view_insert_column_with_attributes(app.proc_tree, -1, "PGID", r_txt, "text", PROC_COL_PGID, NULL);
    gtk_tree_view_insert_column_with_attributes(app.proc_tree, -1, "State", r_txt, "text", PROC_COL_STATE, NULL);
    gtk_tree_view_insert_column_with_attributes(app.proc_tree, -1, "Command", r_txt, "text", PROC_COL_CMD, NULL);

    GtkWidget *scroll_proc = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_size_request(scroll_proc, -1, 90);
    gtk_container_add(GTK_CONTAINER(scroll_proc), GTK_WIDGET(app.proc_tree));
    gtk_box_pack_start(GTK_BOX(right_box), scroll_proc, FALSE, FALSE, 0);

    /* Signal Event Log */
    GtkWidget *lbl_sig = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(lbl_sig), "<b>Signal & Kernel Event Log</b>");
    gtk_box_pack_start(GTK_BOX(right_box), lbl_sig, FALSE, FALSE, 2);

    app.signal_log_view = GTK_TEXT_VIEW(gtk_text_view_new());
    gtk_text_view_set_editable(app.signal_log_view, FALSE);
    app.signal_log_buffer = gtk_text_view_get_buffer(app.signal_log_view);

    GtkWidget *scroll_sig = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_size_request(scroll_sig, -1, 90);
    gtk_container_add(GTK_CONTAINER(scroll_sig), GTK_WIDGET(app.signal_log_view));
    gtk_box_pack_start(GTK_BOX(right_box), scroll_sig, TRUE, TRUE, 0);

    /* 3. Status Bar */
    app.status_label = gtk_label_new("Ready");
    gtk_label_set_xalign(GTK_LABEL(app.status_label), 0.0);
    gtk_box_pack_end(GTK_BOX(vbox_main), app.status_label, FALSE, FALSE, 4);

    /* Attach non-blocking FIFO watch */
    GIOChannel *channel = g_io_channel_unix_new(app.fifo_fd);
    g_io_channel_set_encoding(channel, NULL, NULL);
    g_io_channel_set_flags(channel, G_IO_FLAG_NONBLOCK, NULL);
    g_io_add_watch(channel, G_IO_IN | G_IO_HUP, on_fifo_readable, NULL);

    /* Process polling timer (every 1 second) */
    g_timeout_add(1000, on_proc_timer, NULL);

    log_signal_event("mysh-gui initialized. IPC FIFO connected.");
    update_status_bar();

    gtk_widget_show_all(app.window);
    gtk_main();

    return 0;
}

