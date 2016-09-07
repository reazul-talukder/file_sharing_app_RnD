
#ifndef __MAIN_H__
#define __MAIN_H__

#include <Elementary.h>
#include <app.h>
#include <dlog.h>
#include <efl_extension.h>
#include <storage.h>
#include <bluetooth.h>

#define _PRINT_MSG_LOG_BUFFER_SIZE_ 1024
#define PRINT_MSG(fmt, args...) do { char _log_[_PRINT_MSG_LOG_BUFFER_SIZE_]; \
    snprintf(_log_, _PRINT_MSG_LOG_BUFFER_SIZE_, fmt, ##args); _add_entry_text(_log_); } while (0)

typedef struct {
    Evas_Object *win;
    Evas_Object *navi;
    Evas_Object *navi1;
    Evas_Object *navi2;
    Evas_Object *navi3;
    Evas_Object *navi4;
    Evas_Object *navi5;
    Evas_Object *navi6;
    Evas_Object *navi7;
    Evas_Object *navi8;
    Evas_Object *navi9;
    Evas_Object *navi10;
    Evas_Object *navi12;
    Evas_Object *navi13;
    Evas_Object *buttons[17];
    Ecore_Timer *timer;
    int internal_storage_id;
    bool callbacks_set;

} appdata_s;

void _add_entry_text(const char *text);
void _new_button(appdata_s *ad, Evas_Object *display, char *name, void *cb);
Evas_Object *_create_new_cd_display(appdata_s *ad, char *name, void *cb, Evas_Object *navi);
Eina_Bool _pop_cb(void *data, Elm_Object_Item *item);


#ifndef PACKAGE
#define PACKAGE "org.example.bluetooth"
#endif

#ifdef  LOG_TAG
#undef  LOG_TAG
#endif
#define LOG_TAG "bluetooth"

#endif                           /* __MAIN_H__ */
