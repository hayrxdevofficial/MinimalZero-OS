#pragma once
#include "common.h"

// Clicker
void clicker_init();
void clicker_draw(int cx, int cy, int cw, int ch);
void clicker_click(int px, int py, int cx, int cy, int cw, int ch);
void clicker_tick();

// Calculator
void calc_init();
void calc_draw(int cx, int cy, int cw, int ch);
void calc_click(int px, int py, int cx, int cy, int cw, int ch);
void calc_key(int k);

// Settings
void settings_draw(int cx, int cy, int cw, int ch);
void settings_click(int px, int py, int cx, int cy, int cw, int ch);
void settings_key(int k);

// Files
void files_draw(int cx, int cy, int cw, int ch);
void files_click(int px, int py, int cx, int cy, int cw, int ch);
void files_key(int k);

// Browser
void browser_draw(int cx, int cy, int cw, int ch);
void browser_click(int px, int py, int cx, int cy, int cw, int ch);
void browser_key(int k);

// Installer
void installer_draw(int cx, int cy, int cw, int ch);
void installer_click(int px, int py, int cx, int cy, int cw, int ch);

// CLI
void cli_draw(int cx, int cy, int cw, int ch);
void cli_click(int px, int py, int cx, int cy, int cw, int ch);
void cli_key(int k);