#pragma once

int gui_show_path_dialog(const char *title, const char *initial_path,
                         char *out_path, int out_len);

int gui_show_path_dialog_filtered(const char *title, const char *initial_path,
                                  const char *default_filter,
                                  char *out_path, int out_len);
