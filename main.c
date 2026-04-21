#include "mongoose.h"

static void fn(struct mg_connection *c, int ev, void *ev_data, void *fn_data) {
  if (ev == MG_EV_HTTP_MSG) {
    struct mg_http_message *hm = (struct mg_http_message *) ev_data;
    //if (mg_http_match_uri(hm, "/websocket")) {
    //if (mg_vcmp(&hm->uri, "/websocket") == 0) {
    //if (mg_match_prefix(&hm->uri, "/websocket") == 0) {
    //if (mg_strcmp(&hm->uri, "/websocket") == 0) {
    //mg_match()
    if (mg_match(hm->uri, mg_str("/"), NULL)) {
      mg_ws_upgrade(c, hm, NULL);
    } else {
      mg_http_reply(c, 200, "Content-Type: text/plain\r\n", "WebSocket server\n");
    }
  } else if (ev == MG_EV_WS_OPEN) {
    const char *initial = "{ \"segment\": \"icon_blood\", \"state\": 1 }";
    mg_ws_send(c, initial, strlen(initial), WEBSOCKET_OP_TEXT);
  } else if (ev == MG_EV_WS_MSG) {
    struct mg_ws_message *wm = (struct mg_ws_message *) ev_data;
    mg_ws_send(c, wm->data.buf, wm->data.len, WEBSOCKET_OP_TEXT);
  }
}

int main(void) {
  struct mg_mgr mgr;
  mg_mgr_init(&mgr);
  mg_http_listen(&mgr, "http://localhost:8083", fn, NULL);
  for (;;) mg_mgr_poll(&mgr, 1000);
  mg_mgr_free(&mgr);
  return 0;
}
