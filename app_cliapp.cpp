#include "common.h"

static char cli_lines[100][128];
static int  cli_line_cnt = 0;
static char cli_input[128];
static int  cli_input_len = 0;
static int  cli_dir = 0; // ID папки, в которой находимся

static void cli_add_line(const char* text) {
    if (cli_line_cnt < 100) {
        scopy(cli_lines[cli_line_cnt++], text, 128);
    } else {
        for (int i = 0; i < 99; i++) scopy(cli_lines[i], cli_lines[i+1], 128);
        scopy(cli_lines[99], text, 128);
    }
}

// Парсер команд
static void cli_execute() {
    cli_input[cli_input_len] = 0;
    cli_add_line(cli_input);

    if (seq(cli_input, "help")) {
        cli_add_line("Commands:");
        cli_add_line("  help - this message");
        cli_add_line("  ls - list files");
        cli_add_line("  cd <name> - change dir");
        cli_add_line("  create_app <name> - create app in current dir");
        cli_add_line("  clear - clear screen");
    } 
    else if (seq(cli_input, "ls")) {
        int files[MAXN];
        int n = fs_list(cli_dir, files, MAXN);
        for (int i = 0; i < n; i++) {
            char line[128];
            scopy(line, fs[files[i]].dir ? "[DIR] " : "[FILE] ", 128);
            sappend(line, slen(line), fs[files[i]].name, 128);
            cli_add_line(line);
        }
        if (n == 0) cli_add_line("(empty)");
    }
    else if (cli_input[0] == 'c' && cli_input[1] == 'd' && cli_input[2] == ' ') {
        const char* name = cli_input + 3;
        int files[MAXN];
        int n = fs_list(cli_dir, files, MAXN);
        bool found = false;
        for (int i = 0; i < n; i++) {
            if (fs[files[i]].dir && seq(fs[files[i]].name, name)) {
                cli_dir = files[i];
                found = true;
                break;
            }
        }
        if (!found) cli_add_line("Directory not found");
    }
    else if (cli_input[0] == 'c' && seq(cli_input, "clear")) {
        cli_line_cnt = 0;
    }
    else if (cli_input[0] == 'c' && seq(cli_input, "create_app ")) {
        const char* name = cli_input + 11;
        int new_dir = fs_new(cli_dir, name, true);
        if (new_dir > 0) {
            // Создаем пустые файлы внутри
            int js = fs_new(new_dir, "app.js", false);
            fs_set_txt(js, "// JS code here\n");
            int man = fs_new(new_dir, "manifest.json", false);
            fs_set_txt(man, "{\"name\":\"Test\",\"author\":\"Me\"}");
            int ico = fs_new(new_dir, "icon.png", false);
            fs_set_txt(ico, "PNG DATA");
            cli_add_line("App created successfully!");
            cli_add_line("Files: app.js, manifest.json, icon.png");
        } else {
            cli_add_line("Error: cannot create app");
        }
    }
    else {
        cli_add_line("Unknown command. Type 'help'.");
    }

    cli_input_len = 0;
    cli_input[0] = 0;
}

void cli_draw(int cx, int cy, int cw, int ch) {
    gfx::rect(cx, cy, cw, ch, 0x111111);
    
    // Шапка
    gfx::rect(cx, cy, cw, 24, 0x333333);
    text::draw("iMimi Terminal", cx + 10, cy + 8, 0xFFFFFF, 1);

    // Строки
    int y = cy + 30;
    for (int i = 0; i < cli_line_cnt; i++) {
        text::draw(cli_lines[i], cx + 10, y, 0x00FF00, 1);
        y += 14;
        if (y > cy + ch - 20) break;
    }

    // Ввод
    text::draw("> ", cx + 10, cy + ch - 20, 0xFFFFFF, 1);
    text::draw(cli_input, cx + 28, cy + ch - 20, 0x00FF00, 1);
    if ((time_ms() / 500) % 2 == 0) {
        gfx::rect(cx + 28 + cli_input_len * 8, cy + ch - 20, 8, 12, 0x00FF00);
    }
}

void cli_click(int px, int py, int cx, int cy, int cw, int ch) {
    (void)px; (void)py; (void)cx; (void)cy; (void)cw; (void)ch;
}

void cli_key(int k) {
    if (k == '\n') {
        cli_execute();
    } else if (k == '\b') {
        if (cli_input_len > 0) cli_input[--cli_input_len] = 0;
    } else if (k >= 32 && k < 127 && cli_input_len < 120) {
        cli_input[cli_input_len++] = (char)k;
        cli_input[cli_input_len] = 0;
    }
}