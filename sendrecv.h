#pragma once

#include <glib.h>


void cleanup_and_quit_loop(const gchar * msg, bool is_error);

bool set_connected();
void on_server_message(const gchar *text);
gboolean start_pipeline(gboolean create_offer);

struct ISendRecv;

bool start_sendrecv(unsigned long long winid, ISendRecv* sendrecv);

