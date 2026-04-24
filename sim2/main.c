#include "mongoose.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/time.h>
#endif

typedef struct {
    int measured_value;
    long date_time;  // ddmmyyhhmm
    int serial_number;
} Record;

static Record records[100] = {
    {123, 2304260425L, 1},
    {103, 2304260525L, 2},
    {83,  2304260625L, 3},
    {73,  2304260725L, 4},
    {133, 2304260825L, 5}
};
static int numberofrecords = 5;
static int power_on = 0;        // 0: sleep, 1: on
static int mode = 0;            // 0: sleep, 1: record_view, 2: measure
static int current_record_index = 4;
static struct mg_connection *ws_conn = NULL;

static unsigned long next_state_ms = 0;
static unsigned long next_blink_ms = 0;
static int blink_strip = 0;
static int blink_blood = 0;
static int strip_state = 0;
static int blood_state = 0;
static int countdown_active = 0;
static int countdown_step = 0;

static int pending_record_view = 0;

static unsigned long now_ms(void) {
#ifdef _WIN32
    return GetTickCount();
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (unsigned long)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
#endif
}

static void send_ws_msg(const char *msg) {
    if (ws_conn) {
        mg_ws_send(ws_conn, msg, strlen(msg), WEBSOCKET_OP_TEXT);
    }
}

static void send_group(const char *id, const char *value) {
    char buf[256];
    snprintf(buf, sizeof(buf), "{\"type\":\"group\",\"id\":\"%s\",\"value\":\"%s\"}", id, value);
    send_ws_msg(buf);
}

static void send_icon(const char *id, int state) {
    char buf[128];
    snprintf(buf, sizeof(buf), "{\"type\":\"icon\",\"id\":\"%s\",\"state\":%d}", id, state);
    send_ws_msg(buf);
}

static void clear_lcd(void) {
    send_group("hb-digits", "");
    send_group("hct-digits", "");
    send_group("date-digits", "");
    send_group("time-digits", "");
    send_icon("icon-blood", 0);
    send_icon("icon-strip", 0);
}

static void format_date_time(long dt, char date[7], char timebuf[5]) {
    int minute = dt % 100; dt /= 100;
    int hour = dt % 100; dt /= 100;
    int year = dt % 100; dt /= 100;
    int month = dt % 100; dt /= 100;
    int day = (int)dt;
    snprintf(date, 7, "%02d%02d%02d", day, month, year);
    snprintf(timebuf, 5, "%02d%02d", hour, minute);
}

static void display_record(int index) {
    if (index < 0 || index >= numberofrecords) return;
    clear_lcd();
    Record r = records[index];
    char hb[8];
    char date[7];
    char timebuf[5];
    snprintf(hb, sizeof(hb), "%03d", r.measured_value);
    format_date_time(r.date_time, date, timebuf);
    send_group("hb-digits", hb);
    send_group("date-digits", date);
    send_group("time-digits", timebuf);
}

static void display_current_datetime(void) {
    long dt = 0;
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    dt = (tm->tm_mday * 100000000L) + ((tm->tm_mon + 1) * 1000000L) +
         ((tm->tm_year % 100) * 10000L) + (tm->tm_hour * 100L) + tm->tm_min;
    char date[7];
    char timebuf[5];
    format_date_time(dt, date, timebuf);
    send_group("date-digits", date);
    send_group("time-digits", timebuf);
}

static void send_selftest(void) {
    send_group("hb-digits", "888");
    send_group("hct-digits", "888");
    send_group("date-digits", "888888");
    send_group("time-digits", "8888");
    send_icon("icon-blood", 1);
    send_icon("icon-strip", 1);
    send_icon("icon-battery", 1);
    send_icon("icon-bluetooth", 1);
}

static void start_measure_mode(void) {
    clear_lcd();
    display_current_datetime();
    blink_strip = 1;
    blink_blood = 0;
    strip_state = 0;
    blood_state = 0;
    next_blink_ms = now_ms() + 500;
}

static void start_record_view(void) {
    current_record_index = numberofrecords - 1;
    if (current_record_index < 0) current_record_index = 0;
    display_record(current_record_index);
    mode = 1;
    next_state_ms = now_ms() + 3000;
}

static void start_countdown(void) {
    countdown_active = 1;
    countdown_step = 5;
    next_state_ms = now_ms();
}

static void tick(void) {
    unsigned long now = now_ms();

    if (power_on && pending_record_view && now >= next_state_ms) {
        pending_record_view = 0;
        send_icon("icon-blood", 0);
        send_icon("icon-strip", 0);
        send_icon("icon-battery", 0);
        send_icon("icon-bluetooth", 0);
        start_record_view();
    }

    if (mode == 1 && !pending_record_view && next_state_ms && now >= next_state_ms) {
        clear_lcd();
        mode = 2;
        next_state_ms = 0;
        start_measure_mode();
    }

    if (countdown_active && now >= next_state_ms) {
        if (countdown_step > 0) {
            char buf[4];
            snprintf(buf, sizeof(buf), "%d", countdown_step);
            send_group("hb-digits", buf);
            countdown_step--;
            next_state_ms = now + 300;
        } else {
            countdown_active = 0;
            int hb = 101 + rand() % 70;
            char hbbuf[8];
            snprintf(hbbuf, sizeof(hbbuf), "%03d", hb);
            send_group("hb-digits", hbbuf);
            if (numberofrecords < 100) {
                records[numberofrecords].measured_value = hb;
                records[numberofrecords].date_time = 0;
                time_t t = time(NULL);
                struct tm *tm = localtime(&t);
                records[numberofrecords].date_time =
                    (tm->tm_mday * 100000000L) +
                    ((tm->tm_mon + 1) * 1000000L) +
                    ((tm->tm_year % 100) * 10000L) +
                    (tm->tm_hour * 100L) +
                    tm->tm_min;
                records[numberofrecords].serial_number = numberofrecords + 1;
                numberofrecords++;
            }
        }
    }

    if ((blink_strip || blink_blood) && now >= next_blink_ms) {
        next_blink_ms = now + 500;
        if (blink_strip) {
            strip_state = !strip_state;
            send_icon("icon-strip", strip_state);
        }
        if (blink_blood) {
            blood_state = !blood_state;
            send_icon("icon-blood", blood_state);
        }
    }
}

static void handle_action(const char *action) {
    if (strcmp(action, "BTN_PWR") == 0) {
        power_on = !power_on;
        if (!power_on) {
            mode = 0;
            pending_record_view = 0;
            countdown_active = 0;
            blink_strip = 0;
            blink_blood = 0;
            next_state_ms = 0;
            next_blink_ms = 0;
            clear_lcd();
        } else {
            send_selftest();
            pending_record_view = 1;
            mode = 0;
            next_state_ms = now_ms() + 2000;
        }
    } else if (power_on) {
        if (mode == 1 && strcmp(action, "BTN_UP") == 0) {
            if (current_record_index < numberofrecords - 1) {
                current_record_index++;
                display_record(current_record_index);
                next_state_ms = now_ms() + 3000;
            }
        } else if (mode == 1 && strcmp(action, "BTN_DN") == 0) {
            if (current_record_index > 0) {
                current_record_index--;
                display_record(current_record_index);
                next_state_ms = now_ms() + 3000;
            }
        } else if (mode == 2 && strcmp(action, "SIM_STRIP_IN") == 0) {
            blink_strip = 0;
            send_icon("icon-strip", 1);
            blink_blood = 1;
            blood_state = 0;
            next_blink_ms = now_ms() + 500;
        } else if (mode == 2 && strcmp(action, "SIM_BLOOD") == 0) {
            blink_blood = 0;
            send_icon("icon-blood", 1);
            start_countdown();
        } else if (mode == 2 && strcmp(action, "SIM_STRIP_OUT") == 0) {
            clear_lcd();
            send_icon("icon-blood", 0);
            blink_strip = 1;
            strip_state = 0;
            next_blink_ms = now_ms() + 500;
        }
    }
}

static void fn(struct mg_connection *c, int ev, void *ev_data) {
    if (ev == MG_EV_HTTP_MSG) {
        struct mg_http_message *hm = (struct mg_http_message *)ev_data;
        if (mg_match(hm->uri, mg_str("/"), NULL)) {
            mg_ws_upgrade(c, hm, NULL);
        }
    } else if (ev == MG_EV_WS_OPEN) {
        ws_conn = c;
    } else if (ev == MG_EV_WS_MSG) {
        struct mg_ws_message *wm = (struct mg_ws_message *)ev_data;
        char buf[256];
        int len = wm->data.len;
        if (len >= (int)sizeof(buf)) len = sizeof(buf) - 1;
        memcpy(buf, wm->data.buf, len);
        buf[len] = '\0';
        if (strstr(buf, "\"action\"") != NULL) {
            if (strstr(buf, "BTN_PWR")) handle_action("BTN_PWR");
            else if (strstr(buf, "BTN_UP")) handle_action("BTN_UP");
            else if (strstr(buf, "BTN_DN")) handle_action("BTN_DN");
            else if (strstr(buf, "SIM_STRIP_IN")) handle_action("SIM_STRIP_IN");
            else if (strstr(buf, "SIM_BLOOD")) handle_action("SIM_BLOOD");
            else if (strstr(buf, "SIM_STRIP_OUT")) handle_action("SIM_STRIP_OUT");
        }
    } else if (ev == MG_EV_CLOSE) {
        if (ws_conn == c) ws_conn = NULL;
    }
}

int main(void) {
    srand((unsigned int)time(NULL));
    struct mg_mgr mgr;
    mg_mgr_init(&mgr);
    mg_http_listen(&mgr, "http://localhost:8083", fn, NULL);
    while (1) {
        mg_mgr_poll(&mgr, 100);
        tick();
    }
    mg_mgr_free(&mgr);
    return 0;
}
