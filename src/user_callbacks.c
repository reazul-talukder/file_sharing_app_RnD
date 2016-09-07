/*
 * Copyright (c) 2015 Samsung Electronics Co., Ltd All Rights Reserved
 *
 * Licensed under the Apache License, Version 2.0 (the License);
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an AS IS BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "user_callbacks.h"
#include "main.h"
#include <glib.h>
#include <app_control.h>

GList *devices_list = NULL;
Evas_Object *ap_name_entry = NULL;
Evas_Object *password_entry = NULL;
int my_socket_fd = -1, server_socket_fd = -1;
bt_advertiser_h advertiser = NULL;
bt_gatt_client_h client;
static bt_advertiser_h advertiser_list[3] = { NULL, };

Evas_Object *msg_entry = NULL; //New Text Nox
Evas_Object *msg_entry1 = NULL;


static int advertiser_index = 0;

#define BUFLEN 100

int init_ok = -1;

int int_ok = -1;



const char *contact_number;
const char *contact_name;


static void display_changed_cb(contacts_name_display_order_e name_display_order, void *user_data)
{
    PRINT_MSG("changed display order: %s",
              name_display_order ==
              CONTACTS_NAME_DISPLAY_ORDER_FIRSTLAST ? "CONTACTS_NAME_DISPLAY_ORDER_FIRSTLAST" :
              "CONTACTS_NAME_DISPLAY_ORDER_LASTFIRST");
    dlog_print(DLOG_DEBUG, LOG_TAG, "changed display order: %s",
               name_display_order ==
               CONTACTS_NAME_DISPLAY_ORDER_FIRSTLAST ? "CONTACTS_NAME_DISPLAY_ORDER_FIRSTLAST" :
               "CONTACTS_NAME_DISPLAY_ORDER_LASTFIRST");
}

static void sorting_changed_cb(contacts_name_sorting_order_e name_display_order, void *user_data)
{
    PRINT_MSG("changed sorting order: %s",
              name_display_order ==
              CONTACTS_NAME_SORTING_ORDER_FIRSTLAST ? "CONTACTS_NAME_SORTING_ORDER_FIRSTLAST" :
              "CONTACTS_NAME_SORTING_ORDER_LASTFIRST");
    dlog_print(DLOG_DEBUG, LOG_TAG, "changed sorting order: %s",
               name_display_order ==
               CONTACTS_NAME_SORTING_ORDER_FIRSTLAST ? "CONTACTS_NAME_SORTING_ORDER_FIRSTLAST" :
               "CONTACTS_NAME_SORTING_ORDER_LASTFIRST");
}

//used to free contact records after cleanup
Eina_List *contact_ids = NULL;
Eina_List *group_ids = NULL;

void _person_changed_callback(const char *view_uri, void *user_data)
{
    PRINT_MSG("Person changed");
}

void _group_changed_callback(const char *view_uri, void *user_data)
{
    PRINT_MSG("Group changed");
}

static void contacts_init()
{
    contact_ids = NULL;
    group_ids = NULL;

    init_ok = contacts_connect();

    if (init_ok != CONTACTS_ERROR_NONE) {
        PRINT_MSG("calendar_disconnect() failed");
        dlog_print(DLOG_ERROR, LOG_TAG, "calendar_disconnect() failed: %d", init_ok);
    }

    contacts_setting_add_name_display_order_changed_cb(display_changed_cb, NULL);
    contacts_setting_add_name_sorting_order_changed_cb(sorting_changed_cb, NULL);

    // Monitoring Person Changes
    int error_code =
        contacts_db_add_changed_cb(_contacts_person._uri, _person_changed_callback, NULL);
    if (error_code != CONTACTS_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_db_add_changed_cb() failed: %d", error_code);
        PRINT_MSG("contacts_db_add_changed_cb() failed: %d", error_code);
    }

    // Monitoring Group Changes
    error_code =
        contacts_db_add_changed_cb(_contacts_group._uri, _group_changed_callback, NULL);
    if (error_code != CONTACTS_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_db_add_changed_cb() failed: %d", error_code);
        PRINT_MSG("contacts_db_add_changed_cb() failed: %d", error_code);
    }
}

typedef struct _contacts_gl_person_data {
    int id;
    char *display_name;
    char *default_phone_number;
    contacts_list_h associated_contacts;
} contacts_gl_person_data_t;

static void _free_gl_person_data(contacts_gl_person_data_t *gl_person_data)
{
    if (NULL == gl_person_data)
        return;

    free(gl_person_data->display_name);
    free(gl_person_data->default_phone_number);

    contacts_list_destroy(gl_person_data->associated_contacts, true);
    free(gl_person_data);
}

static bool _get_display_name(contacts_record_h record, char **display_name)
{
    int error_code;

    error_code = contacts_record_get_str(record, _contacts_person.display_name, display_name);
    dlog_print(DLOG_DEBUG, LOG_TAG, "Display name: %s", *display_name);
    if (error_code != CONTACTS_ERROR_NONE)
        return false;

    return true;
}

static void _print_phone_numbers(contacts_list_h associated_contacts)
{
    int error_code;
    contacts_record_h contact;

    while (contacts_list_get_current_record_p(associated_contacts, &contact) == CONTACTS_ERROR_NONE) {
        int i;
        int count = 0;

        contacts_record_get_child_record_count(contact, _contacts_contact.number, &count);

        for (i = 0; i < count; i++) {
            contacts_record_h number = NULL;
            error_code =
                contacts_record_get_child_record_at_p(contact, _contacts_contact.number, i,
                        &number);
            if (error_code != CONTACTS_ERROR_NONE)
                continue;

            int number_id;
            contacts_record_get_int(number, _contacts_number.id, &number_id);
            dlog_print(DLOG_DEBUG, LOG_TAG, "Number id: %d", number_id);
            PRINT_MSG("   Number id: %d", number_id);

            char *number_str = NULL;
            contacts_record_get_str_p(number, _contacts_number.number, &number_str);
            dlog_print(DLOG_DEBUG, LOG_TAG, "Number: %s", number_str);
            PRINT_MSG("   Number: %s", number_str);
        }

        error_code = contacts_list_next(associated_contacts);
        if (error_code != CONTACTS_ERROR_NONE)
            break;
    }
}

static bool _get_associated_contacts(contacts_record_h record,
                                     contacts_list_h *associated_contacts)
{
    int error_code = -1;
    int person_id = -1;
    contacts_query_h query = NULL;
    contacts_filter_h filter = NULL;

    error_code = contacts_record_get_int(record, _contacts_person.id, &person_id);
    if (error_code != CONTACTS_ERROR_NONE)
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_record_get_int() failed: %d", error_code);

    error_code = contacts_query_create(_contacts_contact._uri, &query);
    if (error_code != CONTACTS_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_query_create() failed: %d", error_code);
        return false;
    }

    error_code = contacts_filter_create(_contacts_contact._uri, &filter);
    if (error_code != CONTACTS_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_filter_create() failed: %d", error_code);
        return false;
    }

    error_code =
        contacts_filter_add_int(filter, _contacts_contact.person_id, CONTACTS_MATCH_EQUAL,
                                person_id);
    if (error_code != CONTACTS_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_filter_add_int() failed: %d", error_code);
        return false;
    }

    error_code = contacts_query_set_filter(query, filter);
    if (error_code != CONTACTS_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_query_set_filter() failed: %d", error_code);
        return false;
    }

    error_code = contacts_db_get_records_with_query(query, 0, 0, associated_contacts);
    if (error_code != CONTACTS_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_db_get_records_with_query() failed: %d",
                   error_code);
        return false;
    }

    contacts_filter_destroy(filter);
    contacts_query_destroy(query);
    if (error_code != CONTACTS_ERROR_NONE)
        return false;

    return true;
}

static bool _get_default_phone_number(contacts_record_h record, char **default_phone_number)
{
    contacts_query_h query = NULL;
    contacts_filter_h filter = NULL;
    contacts_list_h list = NULL;
    contacts_record_h record_person_number = NULL;
    int person_id = -1;
    int error_code = CONTACTS_ERROR_NONE;

    error_code += contacts_record_get_int(record, _contacts_person.id, &person_id);

    error_code += contacts_query_create(_contacts_person_number._uri, &query);
    error_code += contacts_filter_create(_contacts_person_number._uri, &filter);
    error_code +=
        contacts_filter_add_bool(filter, _contacts_person_number.is_primary_default, true);
    error_code += contacts_query_set_filter(query, filter);
    error_code += contacts_db_get_records_with_query(query, 0, 0, &list);
    error_code += contacts_list_get_current_record_p(list, &record_person_number);

    error_code +=
        contacts_record_get_str(record_person_number, _contacts_person_number.number,
                                default_phone_number);

    contacts_list_destroy(list, true);
    contacts_filter_destroy(filter);
    contacts_query_destroy(query);
    if (error_code != CONTACTS_ERROR_NONE)
        return false;

    return true;
}

static contacts_gl_person_data_t *_create_gl_person_data(contacts_record_h record)
{
    contacts_gl_person_data_t *gl_person_data;

    gl_person_data = malloc(sizeof(contacts_gl_person_data_t));
    memset(gl_person_data, 0x0, sizeof(contacts_gl_person_data_t));

    if (contacts_record_get_int(record, _contacts_person.id, &gl_person_data->id) !=
            CONTACTS_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "get person id() failed ");
        _free_gl_person_data(gl_person_data);
        return NULL;
    }

    if (false == _get_display_name(record, &gl_person_data->display_name)) {
        dlog_print(DLOG_ERROR, LOG_TAG, "_get_display_name() failed ");
        _free_gl_person_data(gl_person_data);
        return NULL;
    }

    if (false == _get_default_phone_number(record, &gl_person_data->default_phone_number)) {
        dlog_print(DLOG_ERROR, LOG_TAG, "_get_default_phone_number() failed ");
        _free_gl_person_data(gl_person_data);
        return NULL;
    }

    if (false == _get_associated_contacts(record, &gl_person_data->associated_contacts)) {
        dlog_print(DLOG_ERROR, LOG_TAG, "_get_associated_contacts() failed ");
        _free_gl_person_data(gl_person_data);
        return NULL;
    }

    return gl_person_data;
}


void _send(appdata_s *ad, Evas_Object *obj, void *event_info)
{
    // Setting the window
    ad->win = elm_win_util_standard_add(PACKAGE, PACKAGE);
    elm_win_conformant_set(ad->win, EINA_TRUE);
    elm_win_autodel_set(ad->win, EINA_TRUE);
    elm_win_indicator_mode_set(ad->win, ELM_WIN_INDICATOR_SHOW);
    elm_win_indicator_opacity_set(ad->win, ELM_WIN_INDICATOR_OPAQUE);



		Evas_Object *conform = elm_conformant_add(ad->win);

	    evas_object_size_hint_weight_set(conform, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
	    elm_win_resize_object_add(ad->win, conform);
	    evas_object_show(conform);

	// Create a naviframe
	    ad->navi10 = elm_naviframe_add(conform);
	    evas_object_size_hint_align_set(ad->navi10, EVAS_HINT_FILL, EVAS_HINT_FILL);
	    evas_object_size_hint_weight_set(ad->navi10, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);

	    elm_object_content_set(conform, ad->navi10);
	    evas_object_show(ad->navi10);

	    // Fill the list with items
	  create_buttons_in_main_window19(ad,obj,event_info);


	    eext_object_event_callback_add(ad->navi10, EEXT_CALLBACK_BACK, eext_naviframe_back_cb, NULL);

	    // Show the window after base gui is set up
	    evas_object_show(ad->win);


}

void create_buttons_in_main_window19(appdata_s *ad, Evas_Object *obj, void *event_info){

	Evas_Object *display = _create_new_cd_display(ad, "Sending or Receiving contact", _pop_cb,ad->navi10);

	const char *service_uuid = "00001101-0000-1000-8000-00805F9B34FB";

	    elm_object_text_set(obj, "Send message through socket");

	    /* Send message if already connected to the server */
	    if (server_socket_fd != -1) {
	    	//MODIFIED
	    	char data1[100];

	    	const char *data_msg1 = elm_entry_entry_get(msg_entry1); //Getting text from newly added text box

	    	PRINT_MSG("Sending message... [%s]", data_msg1);

	    	sprintf(data1, "%s", data_msg1);

	    	PRINT_MSG("Msg... [%s]", data1);


	        /* Send data */
	        int ret = bt_socket_send_data(server_socket_fd, data1, sizeof(data1));
	        if (ret < 0) {
	            PRINT_MSG("[bt_socket_send_data] failed.");
	            dlog_print(DLOG_ERROR, LOG_TAG, "[bt_socket_send_data] failed.");
	        } else
	            return;
	    } else {
	        const char *remote_server_address = elm_entry_entry_get(ap_name_entry);

	        if (!strcmp(remote_server_address, "")) {
	            PRINT_MSG
	            ("Enter other device MAC address. This device needs to have this application launched.");
	            return;
	        }

	        /* Request a connection to the Bluetooth server */
	        PRINT_MSG("Connecting to the server...");
	        int ret = bt_socket_connect_rfcomm(remote_server_address, service_uuid);
	        if (ret != BT_ERROR_NONE) {
	            PRINT_MSG("[bt_socket_connect_rfcomm] failed.");
	            dlog_print(DLOG_ERROR, LOG_TAG, "[bt_socket_connect_rfcomm] failed.");
	            return;
	        } else {
	            PRINT_MSG
	            ("[bt_socket_connect_rfcomm] Succeeded. bt_socket_connection_state_changed_cb will be called.");
	            dlog_print(DLOG_DEBUG, LOG_TAG,
	                       "[bt_socket_connect_rfcomm] Succeeded. bt_socket_connection_state_changed_cb will be called.");
	        }
	    }

}


void _save(appdata_s *ad, Evas_Object *obj, void *event_info)
{
	 	 // create contact record
	    	 contacts_record_h contact = NULL;
	    	 contacts_record_create(_contacts_contact._uri, &contact);

	    	 // add name
	    	 contacts_record_h name = NULL;
	    	 contacts_record_create(_contacts_name._uri, &name);
	    	 contacts_record_set_str(name, _contacts_name.first, contact_name);
	    	 contacts_record_add_child_record(contact, _contacts_contact.name, name);

	    	 // add number
	    	 contacts_record_h number = NULL;
	    	 contacts_record_create(_contacts_number._uri, &number);
	    	 contacts_record_set_str(number, _contacts_number.number, contact_number);
	    	 contacts_record_add_child_record(contact, _contacts_contact.number, number);

	    	 // insert to database
	    	 int contact_id = 0;
	    	 contacts_db_insert_record(contact, &contact_id);

	    	 // destroy record
	    	 contacts_record_destroy(contact, true);



}



void create_buttons_in_main_window12(appdata_s *ad, Evas_Object *obj, void *event_info){

	Evas_Object *display = _create_new_cd_display(ad, "Managing Contacts", _pop_cb,ad->navi12);


    if (init_ok != 0)
        return;

    contacts_record_h contact;

    // Creating a Contact
    int error_code = contacts_record_create(_contacts_contact._uri, &contact);
    if (error_code != CONTACTS_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_record_create() failed: %d", error_code);
        PRINT_MSG("contacts_record_create() failed: %d", error_code);
        return;
    }

    // Setting Contact Properties
    contacts_record_h name;
    error_code = contacts_record_create(_contacts_name._uri, &name);
    if (error_code != CONTACTS_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_record_create() failed: %d", error_code);
        PRINT_MSG("contacts_record_create() failed: %d", error_code);
        return;
    }

    error_code = contacts_record_set_str(name, _contacts_name.first, "John");
    if (error_code != CONTACTS_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_record_set_str() failed: %d", error_code);
        PRINT_MSG("contacts_record_set_str() failed: %d", error_code);
    }

    error_code = contacts_record_set_str(name, _contacts_name.last, "Smith");
    if (error_code != CONTACTS_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_record_set_str() failed: %d", error_code);
        PRINT_MSG("contacts_record_set_str() failed: %d", error_code);
    }

    error_code = contacts_record_add_child_record(contact, _contacts_contact.name, name);
    if (error_code != CONTACTS_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_record_add_child_record() failed: %d",
                   error_code);
        PRINT_MSG("contacts_record_add_child_record() failed: %d", error_code);
    }

    contacts_record_h image = NULL;

    error_code = contacts_record_create(_contacts_image._uri, &image);
    if (error_code != CONTACTS_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_record_create() failed: %d", error_code);
        PRINT_MSG("contacts_record_create() failed: %d", error_code);
        return;
    }

    char image_path[BUFLEN];
    char *shared_path = app_get_shared_resource_path();
    snprintf(image_path, BUFLEN, "%ssample.jpg", shared_path);
    free(shared_path);

    error_code = contacts_record_set_str(image, _contacts_image.path, image_path);
    if (error_code != CONTACTS_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_record_set_str() failed: %d", error_code);
        PRINT_MSG("contacts_record_set_str() failed: %d", error_code);
    }

    error_code = contacts_record_add_child_record(contact, _contacts_contact.image, image);
    if (error_code != CONTACTS_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_record_add_child_record() failed: %d",
                   error_code);
        PRINT_MSG("contacts_record_add_child_record() failed: %d", error_code);
    }

    contacts_record_h event = NULL;

    error_code = contacts_record_create(_contacts_event._uri, &event);
    if (error_code != CONTACTS_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_record_create() failed: %d", error_code);
        PRINT_MSG("contacts_record_create() failed: %d", error_code);
        return;
    }

    int year = 1990;
    int month = 5;
    int day = 21;
    int int_date = year * 10000 + month * 100 + day;

    error_code = contacts_record_set_int(event, _contacts_event.date, int_date);
    if (error_code != CONTACTS_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_record_set_int() failed: %d", error_code);
        PRINT_MSG("contacts_record_set_int() failed: %d", error_code);
    }

    error_code = contacts_record_set_int(event, _contacts_event.type, CONTACTS_EVENT_TYPE_BIRTH);
    if (error_code != CONTACTS_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_record_set_int() failed: %d", error_code);
        PRINT_MSG("contacts_record_set_int() failed: %d", error_code);
    }

    error_code = contacts_record_set_int(event, _contacts_event.type, CONTACTS_EVENT_TYPE_CUSTOM);
    if (error_code != CONTACTS_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_record_set_int() failed: %d", error_code);
        PRINT_MSG("contacts_record_set_int() failed: %d", error_code);
    }

    error_code = contacts_record_set_str(event, _contacts_event.label, "Event description");
    if (error_code != CONTACTS_ERROR_NONE) {
        PRINT_MSG("contacts_record_set_str() failed: %d", error_code);
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_record_set_str() failed: %d", error_code);
    }

    error_code = contacts_record_add_child_record(contact, _contacts_contact.event, event);
    if (error_code != CONTACTS_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_record_add_child_record() failed: %d",
                   error_code);
        PRINT_MSG("contacts_record_add_child_record() failed: %d", error_code);
    }

    contacts_record_h number;

    error_code = contacts_record_create(_contacts_number._uri, &number);
    if (error_code != CONTACTS_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_record_create() failed: %d", error_code);
        PRINT_MSG("contacts_record_create() failed: %d", error_code);
    }

    error_code = contacts_record_set_str(number, _contacts_number.number, "+8210-1234-5678");
    if (error_code != CONTACTS_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_record_set_str() failed: %d", error_code);
        PRINT_MSG("contacts_record_set_str() failed: %d", error_code);
    }

    error_code = contacts_record_add_child_record(contact, _contacts_contact.number, number);
    if (error_code != CONTACTS_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_record_add_child_record() failed: %d",
                   error_code);
        PRINT_MSG("contacts_record_add_child_record() failed: %d", error_code);
    }

    // Inserting a Contact to the Database
    PRINT_MSG("Inserting a contact to the database");
    int id = -1;
    error_code = contacts_db_insert_record(contact, &id);
    if (error_code != CONTACTS_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_db_insert_record() failed: %d", error_code);
        PRINT_MSG("contacts_db_insert_record() failed: %d", error_code);
    }

    contact_ids = eina_list_append(contact_ids, (const void *)id);
    contacts_record_destroy(contact, true);

    // here insert once more to show how works persons linking
    contacts_record_h contact2 = NULL;
    error_code = contacts_record_create(_contacts_contact._uri, &contact2);
    if (error_code != CONTACTS_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_record_create() failed: %d", error_code);
        PRINT_MSG("contacts_record_create() failed: %d", error_code);
        return;
    }

    int tmp_contact = -1;
    error_code = contacts_db_insert_record(contact2, &tmp_contact);
    if (error_code != CONTACTS_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_db_insert_record() failed: %d", error_code);
        PRINT_MSG("contacts_db_insert_record() failed: %d", error_code);
    }

    contact_ids = eina_list_append(contact_ids, (const void *)tmp_contact);

    contacts_record_destroy(contact2, true);

    int person_id;
    // person_id = id of person - person_id must be set to an id of a record in the _contacts_person view.

    // Getting Contacts
    PRINT_MSG("");
    PRINT_MSG("List contacts");
    contacts_list_h list = NULL;

    error_code = contacts_db_get_all_records(_contacts_person._uri, 0, 0, &list);
    if (error_code != CONTACTS_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_db_get_all_records() failed: %d", error_code);
        PRINT_MSG("contacts_db_get_all_records() failed: %d", error_code);
    }

    contacts_list_destroy(list, true);

    contacts_query_h query = NULL;

    error_code = contacts_query_create(_contacts_person._uri, &query);
    if (error_code != CONTACTS_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_query_create() failed: %d", error_code);
        PRINT_MSG("contacts_query_create() failed: %d", error_code);
    }

    unsigned int fields[] = { _contacts_person.display_name, _contacts_person.is_favorite };

    error_code = contacts_query_set_projection(query, fields, 1);
    if (error_code != CONTACTS_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_query_set_projection() failed: %d", error_code);
        PRINT_MSG("contacts_query_set_projection() failed: %d", error_code);
    }

    contacts_filter_h filter = NULL;

    error_code = contacts_filter_create(_contacts_person._uri, &filter);
    if (error_code != CONTACTS_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_filter_create() failed: %d", error_code);
        PRINT_MSG("contacts_filter_create() failed: %d", error_code);
    }

    error_code =
        contacts_filter_add_str(filter, _contacts_person.display_name, CONTACTS_MATCH_CONTAINS,
                                "John");
    if (error_code != CONTACTS_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_filter_add_str() failed: %d", error_code);
        PRINT_MSG("contacts_filter_add_str() failed: %d", error_code);
    }

    error_code = contacts_filter_add_operator(filter, CONTACTS_FILTER_OPERATOR_AND);
    if (error_code != CONTACTS_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_filter_add_operator() failed: %d", error_code);
        PRINT_MSG("contacts_filter_add_operator() failed: %d", error_code);
    }

    error_code = contacts_filter_add_bool(filter, _contacts_person.is_favorite, true);
    if (error_code != CONTACTS_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_filter_add_bool() failed: %d", error_code);
        PRINT_MSG("contacts_filter_add_bool() failed: %d", error_code);
    }

    error_code = contacts_query_set_filter(query, filter);
    if (error_code != CONTACTS_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_query_set_filter() failed: %d", error_code);
        PRINT_MSG("contacts_query_set_filter() failed: %d", error_code);
    }

    error_code = contacts_db_get_records_with_query(query, 0, 0, &list);
    if (error_code != CONTACTS_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_db_get_records_with_query() failed: %d",
                   error_code);
        PRINT_MSG("contacts_db_get_records_with_query() failed: %d", error_code);
    }

    contacts_query_destroy(query);
    contacts_filter_destroy(filter);

    error_code = contacts_db_search_records(_contacts_person._uri, "John", 0, 0, &list);
    if (error_code != CONTACTS_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_db_search_records() failed: %d", error_code);
        PRINT_MSG("contacts_db_search_records() failed: %d", error_code);
    }

    contacts_record_h record;

    while (contacts_list_get_current_record_p(list, &record) == CONTACTS_ERROR_NONE) {
        char *display_name;
        error_code =
            contacts_record_get_str_p(record, _contacts_person.display_name, &display_name);
        if (error_code != CONTACTS_ERROR_NONE) {
            PRINT_MSG("contacts_record_get_str_p() failed");
            dlog_print(DLOG_ERROR, LOG_TAG, "contacts_record_get_str_p() failed");
            break;
        }

        dlog_print(DLOG_DEBUG, LOG_TAG, "Display name: %s", display_name);
        PRINT_MSG(" - Display name: %s", display_name);

        error_code = contacts_list_next(list);
        if (error_code != CONTACTS_ERROR_NONE)
            break;
    }

    PRINT_MSG("");

    PRINT_MSG("Search John contact");
    contacts_list_first(list);
    // Another possibility to get information through contacts_gl_person_data_t structure
    contacts_gl_person_data_t *gl_person_data = NULL;

    error_code = contacts_db_search_records(_contacts_person._uri, "John", 0, 0, &list);
    if (error_code != CONTACTS_ERROR_NONE) {
        PRINT_MSG("contacts_db_search_records() failed: %d", error_code);
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_db_search_records() failed: %d", error_code);
    }

    while (contacts_list_get_current_record_p(list, &record) == CONTACTS_ERROR_NONE) {
        gl_person_data = _create_gl_person_data(record);
        // You can get, for example, display name:
        PRINT_MSG(" - Display name: %s", gl_person_data->display_name);

        _print_phone_numbers(gl_person_data->associated_contacts);

        error_code = contacts_list_next(list);
        if (error_code != CONTACTS_ERROR_NONE)
            break;
    }

    contacts_list_destroy(list, true);

    PRINT_MSG("");

    // Updating a Contact
    int contact_id = id;

    error_code = contacts_db_get_record(_contacts_contact._uri, contact_id, &contact);
    if (error_code != CONTACTS_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_db_get_record() failed: %d", error_code);
        PRINT_MSG("contacts_db_get_record() failed: %d", error_code);
    }

    error_code = contacts_record_get_int(contact, _contacts_contact.person_id, &person_id);
    if (error_code != CONTACTS_ERROR_NONE)
        PRINT_MSG("contacts_record_get_int() failed: %d", error_code);

    name = NULL;
    error_code = contacts_record_get_child_record_at_p(contact, _contacts_contact.name, 0, &name);
    if (error_code != CONTACTS_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_record_get_child_record_at_p() failed: %d",
                   error_code);
        PRINT_MSG("contacts_record_get_child_record_at_p() failed: %d", error_code);
    }

    error_code = contacts_record_set_str(name, _contacts_name.first, "Mark");
    if (error_code != CONTACTS_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_record_set_str() failed: %d", error_code);
        PRINT_MSG("contacts_record_set_str() failed: %d", error_code);
    }

    event = NULL;
    error_code = contacts_record_get_child_record_at_p(contact, _contacts_contact.event, 0, &event);
    if (error_code != CONTACTS_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_record_get_child_record_at_p() failed: %d",
                   error_code);
        PRINT_MSG("contacts_record_get_child_record_at_p() failed: %d", error_code);
    }

    int new_date = 1990 * 10000 + 6 * 100 + 21;

    error_code = contacts_record_set_int(event, _contacts_event.date, new_date);
    if (error_code != CONTACTS_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_record_set_int() failed: %d", error_code);
        PRINT_MSG("contacts_record_set_int() failed: %d", error_code);
    }

    PRINT_MSG("Update contact");
    error_code = contacts_db_update_record(contact);
    if (error_code != CONTACTS_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_db_update_record() failed: %d", error_code);
        PRINT_MSG("contacts_db_update_record() failed: %d", error_code);
    }

    contacts_record_destroy(contact, true);
    PRINT_MSG("");

    // Linking and Unlinking Contacts
    PRINT_MSG("Persons linking");
    int person_id2 = person_id + 1;

    error_code = contacts_person_link_person(person_id, person_id2);
    if (error_code != CONTACTS_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_person_link_person() failed: %d", error_code);
        PRINT_MSG("contacts_person_link_person() failed: %d", error_code);
    }

    contact = NULL;


    PRINT_MSG("");

    // Managing Favorites

    contact = NULL;

    error_code = contacts_record_create(_contacts_contact._uri, &contact);
    if (error_code != CONTACTS_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_record_create() failed: %d", error_code);
        PRINT_MSG("contacts_record_create() failed: %d", error_code);
    }

    error_code = contacts_record_set_bool(contact, _contacts_contact.is_favorite, true);
    if (error_code != CONTACTS_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_record_set_bool() failed: %d", error_code);
        PRINT_MSG("contacts_record_set_bool() failed: %d", error_code);
    }

    PRINT_MSG("Setting person as favourite");

    // A person is set as favorite
    contacts_record_h person = NULL;

    error_code = contacts_db_get_record(_contacts_person._uri, person_id, &person);
    if (error_code != CONTACTS_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_db_get_record() failed: %d", error_code);
        PRINT_MSG("contacts_db_get_record() failed: %d", error_code);
    }

    error_code = contacts_record_set_bool(person, _contacts_person.is_favorite, true);
    if (error_code != CONTACTS_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_record_set_bool() failed: %d", error_code);
        PRINT_MSG("contacts_record_set_bool() failed: %d", error_code);
    }

    error_code = contacts_db_update_record(person);
    if (error_code != CONTACTS_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_db_update_record() failed: %d", error_code);
        PRINT_MSG("contacts_db_update_record() failed: %d", error_code);
    }

    contacts_record_destroy(person, true);

    PRINT_MSG("");
    PRINT_MSG("Setting person remove callback");


}



typedef struct _contacts_gl_group_data {
    int id;
    char *name;
    char *image_path;
    char *ringtone_path;
} contacts_gl_group_data_t;

void _free_gl_group_data(contacts_gl_group_data_t *gl_group_data)
{
    if (NULL == gl_group_data)
        return;

    free(gl_group_data->name);
    free(gl_group_data->image_path);
    free(gl_group_data->ringtone_path);
    free(gl_group_data);
}

static contacts_gl_group_data_t *_create_gl_group_data(contacts_record_h record)
{
    contacts_gl_group_data_t *gl_group_data = NULL;

    gl_group_data = malloc(sizeof(contacts_gl_group_data_t));
    memset(gl_group_data, 0x0, sizeof(contacts_gl_group_data_t));

    if (CONTACTS_ERROR_NONE !=
            contacts_record_get_int(record, _contacts_group.id, &gl_group_data->id)) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_record_get_int() failed ");
        _free_gl_group_data(gl_group_data);
        return NULL;
    }

    if (CONTACTS_ERROR_NONE !=
            contacts_record_get_str(record, _contacts_group.name, &gl_group_data->name)) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_record_get_str() failed ");
        _free_gl_group_data(gl_group_data);
        return NULL;
    }

    if (CONTACTS_ERROR_NONE !=
            contacts_record_get_str(record, _contacts_group.image_path, &gl_group_data->image_path)) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_record_get_str() failed ");
        _free_gl_group_data(gl_group_data);
        return NULL;
    }

    if (CONTACTS_ERROR_NONE !=
            contacts_record_get_str(record, _contacts_group.ringtone_path,
                                    &gl_group_data->ringtone_path)) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_record_get_str() failed ");
        _free_gl_group_data(gl_group_data);
        return NULL;
    }

    return gl_group_data;
}

contacts_gl_group_data_t *_gl_group_data = NULL;

void create_buttons_in_main_window13(appdata_s *ad, Evas_Object *obj, void *event_info){

	Evas_Object *display = _create_new_cd_display(ad, "Group Management", _pop_cb,ad->navi13);

	 if (init_ok != 0)
	        return;

	    // Creating a Group
	    contacts_record_h group = NULL;

	    int error_code = contacts_record_create(_contacts_group._uri, &group);
	    if (error_code != CONTACTS_ERROR_NONE) {
	        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_record_create() failed: %d", error_code);
	        PRINT_MSG("contacts_record_create() failed: %d", error_code);
	        return;
	    }

	    // Setting Group Properties
	    error_code = contacts_record_set_str(group, _contacts_group.name, "Neighbors");
	    if (error_code != CONTACTS_ERROR_NONE) {
	        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_record_set_str() failed: %d", error_code);
	        PRINT_MSG("contacts_record_set_str() failed: %d", error_code);
	    }

	    char image_path[BUFLEN];
	    char *shared_path = app_get_shared_resource_path();
	    snprintf(image_path, BUFLEN, "%ssample.jpg", shared_path);

	    error_code = contacts_record_set_str(group, _contacts_group.image_path, image_path);
	    if (error_code != CONTACTS_ERROR_NONE) {
	        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_record_set_str() failed: %d", error_code);
	        PRINT_MSG("contacts_record_set_str() failed: %d", error_code);
	    }

	    char sound_path[BUFLEN];
	    snprintf(sound_path, BUFLEN, "%stest1.wav", shared_path);
	    free(shared_path);
	    error_code = contacts_record_set_str(group, _contacts_group.ringtone_path, sound_path);
	    if (error_code != CONTACTS_ERROR_NONE) {
	        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_record_set_str() failed: %d", error_code);
	        PRINT_MSG("contacts_record_set_str() failed: %d", error_code);
	    }

	    // Inserting a Group to the Database
	    int added_group_id = -1;

	    error_code = contacts_db_insert_record(group, &added_group_id);
	    if (error_code != CONTACTS_ERROR_NONE) {
	        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_record_set_str() failed: %d", error_code);
	        PRINT_MSG("contacts_record_set_str() failed: %d", error_code);
	    }

	    group_ids = eina_list_append(group_ids, (const void *)added_group_id);

	    contacts_record_destroy(group, true);

	    // Getting Groups
	    group = NULL;
	    int group_id = added_group_id;
	    error_code = contacts_db_get_record(_contacts_group._uri, group_id, &group);
	    if (error_code != CONTACTS_ERROR_NONE) {
	        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_db_get_record() failed: %d", error_code);
	        PRINT_MSG("contacts_db_get_record() failed: %d", error_code);
	    }

	    contacts_record_destroy(group, true);

	    contacts_list_h list = NULL;
	    error_code = contacts_db_get_all_records(_contacts_group._uri, 0, 0, &list);
	    if (error_code != CONTACTS_ERROR_NONE) {
	        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_db_get_all_records() failed: %d", error_code);
	        PRINT_MSG("contacts_db_get_all_records() failed: %d", error_code);
	    }

	    contacts_list_destroy(list, true);

	    contacts_query_h query = NULL;

	    error_code = contacts_query_create(_contacts_group._uri, &query);
	    if (error_code != CONTACTS_ERROR_NONE) {
	        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_query_create() failed: %d", error_code);
	        PRINT_MSG("contacts_query_create() failed: %d", error_code);
	    }

	    contacts_filter_h filter = NULL;

	    error_code = contacts_filter_create(_contacts_group._uri, &filter);
	    if (error_code != CONTACTS_ERROR_NONE) {
	        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_filter_create() failed: %d", error_code);
	        PRINT_MSG("contacts_filter_create() failed: %d", error_code);
	    }

	    error_code =
	        contacts_filter_add_str(filter, _contacts_group.name, CONTACTS_MATCH_CONTAINS, "neighbors");
	    if (error_code != CONTACTS_ERROR_NONE) {
	        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_filter_add_str() failed: %d", error_code);
	        PRINT_MSG("contacts_filter_add_str() failed: %d", error_code);
	    }

	    error_code = contacts_filter_add_operator(filter, CONTACTS_FILTER_OPERATOR_OR);
	    if (error_code != CONTACTS_ERROR_NONE) {
	        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_filter_add_operator() failed: %d", error_code);
	        PRINT_MSG("contacts_filter_add_operator() failed: %d", error_code);
	    }

	    error_code =
	        contacts_filter_add_str(filter, _contacts_group.name, CONTACTS_MATCH_CONTAINS, "friend");
	    if (error_code != CONTACTS_ERROR_NONE) {
	        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_filter_add_str() failed: %d", error_code);
	        PRINT_MSG("contacts_filter_add_str() failed: %d", error_code);
	    }

	    error_code = contacts_query_set_filter(query, filter);
	    if (error_code != CONTACTS_ERROR_NONE) {
	        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_set_filter() failed: %d", error_code);
	        PRINT_MSG("contacts_set_filter() failed: %d", error_code);
	    }

	    error_code = contacts_db_get_records_with_query(query, 0, 0, &list);
	    if (error_code != CONTACTS_ERROR_NONE) {
	        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_db_get_records_with_query() failed: %d",
	                   error_code);
	        PRINT_MSG("contacts_db_get_records_with_query() failed: %d", error_code);
	    }

	    contacts_filter_destroy(filter);
	    contacts_query_destroy(query);

	    PRINT_MSG("List groups:");

	    contacts_record_h record;

	    contacts_gl_group_data_t *gl_group_data = NULL;

	    while (contacts_list_get_current_record_p(list, &record) == CONTACTS_ERROR_NONE) {
	        gl_group_data = _create_gl_group_data(record);

	        if (!gl_group_data) {
	            PRINT_MSG("Creating group data failed");
	            break;
	        }

	        // You can get, for example, display name:
	        PRINT_MSG(" - Group name: %s", gl_group_data->name);

	        error_code = contacts_list_next(list);
	        if (error_code != CONTACTS_ERROR_NONE)
	            break;
	    }

	    contacts_list_first(list);

	    PRINT_MSG("");
	    PRINT_MSG("List groups again:");

	    // Alternative: Using contacts_record_get_* instead of structure like in example below the comment

	    while (contacts_list_get_current_record_p(list, &record) == CONTACTS_ERROR_NONE) {
	        char *name;
	        error_code = contacts_record_get_str_p(record, _contacts_group.name, &name);
	        PRINT_MSG(" - Group name: %s", name);

	        error_code = contacts_list_next(list);
	        if (error_code != CONTACTS_ERROR_NONE)
	            break;
	    }

	    contacts_list_destroy(list, true);
	    PRINT_MSG("");

	    // Updating a Group
	    group_id = added_group_id;  // Acquire group ID
	    group = NULL;

	    error_code = contacts_db_get_record(_contacts_group._uri, group_id, &group);
	    if (error_code != CONTACTS_ERROR_NONE) {
	        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_db_get_record() failed: %d", error_code);
	        PRINT_MSG("contacts_db_get_record() failed: %d", error_code);
	    }

	    error_code = contacts_record_set_str(group, _contacts_group.name, "Family");
	    if (error_code != CONTACTS_ERROR_NONE) {
	        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_record_set_str() failed: %d", error_code);
	        PRINT_MSG("contacts_record_set_str() failed: %d", error_code);
	    }

	    error_code = contacts_record_set_str(group, _contacts_group.image_path, image_path);
	    if (error_code != CONTACTS_ERROR_NONE) {
	        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_record_set_str() failed: %d", error_code);
	        PRINT_MSG("contacts_record_set_str() failed: %d", error_code);
	    }

	    PRINT_MSG("Update group");
	    error_code = contacts_db_update_record(group);
	    if (error_code != CONTACTS_ERROR_NONE) {
	        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_db_update_record() failed: %d", error_code);
	        PRINT_MSG("contacts_db_update_record() failed: %d", error_code);
	    }

	    contacts_record_destroy(group, true);
	    PRINT_MSG("");

	    group_id = added_group_id;

	    // Managing Group Members
	    int contact_id;
	    contacts_record_h contact = NULL;
	    contacts_record_create(_contacts_contact._uri, &contact);
	    contacts_record_h name;

	    error_code = contacts_record_create(_contacts_name._uri, &name);
	    if (error_code != CONTACTS_ERROR_NONE) {
	        PRINT_MSG("contacts_record_create() failed: %d", error_code);
	        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_record_create() failed: %d", error_code);
	    }

	    error_code = contacts_record_set_str(name, _contacts_name.first, "John");
	    if (error_code != CONTACTS_ERROR_NONE) {
	        PRINT_MSG("contacts_record_set_str() failed: %d", error_code);
	        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_record_set_str() failed: %d", error_code);
	    }

	    error_code = contacts_record_set_str(name, _contacts_name.last, "Smith");
	    if (error_code != CONTACTS_ERROR_NONE) {
	        PRINT_MSG("contacts_record_set_str() failed: %d", error_code);
	        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_record_set_str() failed: %d", error_code);
	    }

	    error_code = contacts_record_add_child_record(contact, _contacts_contact.name, name);
	    if (error_code != CONTACTS_ERROR_NONE) {
	        PRINT_MSG("contacts_record_add_child_record() failed: %d", error_code);
	        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_record_add_child_record() failed: %d",
	                   error_code);
	    }

	    PRINT_MSG("Add contact to the group");
	    error_code = contacts_db_insert_record(contact, &contact_id);
	    if (error_code != CONTACTS_ERROR_NONE) {
	        PRINT_MSG("contacts_db_insert_record() failed: %d", error_code);
	        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_db_insert_record() failed: %d", error_code);
	    }

	    contact_ids = eina_list_append(contact_ids, (const void *)contact_id);

	    contacts_record_destroy(contact, true);

	    error_code = contacts_group_add_contact(group_id, contact_id);
	    if (error_code != CONTACTS_ERROR_NONE) {
	        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_group_add_contact() failed: %d", error_code);
	        PRINT_MSG("contacts_group_add_contact() failed: %d", error_code);
	    }

	    group_id = added_group_id;
	    PRINT_MSG("");

	    list = NULL;

	    error_code = contacts_query_create(_contacts_person_group_assigned._uri, &query);
	    if (error_code != CONTACTS_ERROR_NONE) {
	        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_query_create() failed: %d", error_code);
	        PRINT_MSG("contacts_query_create() failed: %d", error_code);
	    }

	    error_code = contacts_filter_create(_contacts_person_group_assigned._uri, &filter);
	    if (error_code != CONTACTS_ERROR_NONE) {
	        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_filter_create() failed: %d", error_code);
	        PRINT_MSG("contacts_filter_create() failed: %d", error_code);
	    }

	    error_code =
	        contacts_filter_add_int(filter, _contacts_person_group_assigned.group_id,
	                                CONTACTS_MATCH_EQUAL, group_id);
	    if (error_code != CONTACTS_ERROR_NONE) {
	        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_filter_add_int() failed: %d", error_code);
	        PRINT_MSG("contacts_filter_add_int() failed: %d", error_code);
	    }

	    error_code = contacts_query_set_filter(query, filter);
	    if (error_code != CONTACTS_ERROR_NONE) {
	        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_query_set_filter() failed: %d", error_code);
	        PRINT_MSG("contacts_query_set_filter() failed: %d", error_code);
	    }

	    error_code = contacts_db_get_records_with_query(query, 0, 0, &list);
	    if (error_code != CONTACTS_ERROR_NONE) {
	        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_db_get_records_with_query() failed: %d",
	                   error_code);
	        PRINT_MSG("contacts_db_get_records_with_query() failed: %d", error_code);
	    }

	    contacts_filter_destroy(filter);
	    contacts_record_h person = NULL;
	    PRINT_MSG("List of person from new created group:");

	    while (contacts_list_get_current_record_p(list, &person) == CONTACTS_ERROR_NONE) {
	        int person_id;
	        contacts_record_get_int(person, _contacts_person_group_assigned.person_id, &person_id);
	        PRINT_MSG("- Person id: %d", person_id);
	        dlog_print(DLOG_DEBUG, LOG_TAG, "Person id: %d", person_id);
	        char *display_name;
	        contacts_record_get_str_p(person, _contacts_person_group_assigned.display_name,
	                                  &display_name);
	        PRINT_MSG("  Display name: %s", display_name);
	        dlog_print(DLOG_DEBUG, LOG_TAG, "Display name: %s", display_name);

	        error_code = contacts_list_next(list);
	        if (error_code != CONTACTS_ERROR_NONE)
	            break;
	    }

	    error_code = contacts_group_remove_contact(group_id, contact_id);
	    if (error_code != CONTACTS_ERROR_NONE) {
	        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_group_remove_contact() failed: %d", error_code);
	        PRINT_MSG("contacts_group_remove_contact() failed: %d", error_code);
	    }

	    PRINT_MSG("");
	    PRINT_MSG("Add callback tracking changes in groups");

	    _free_gl_group_data(_gl_group_data);
	    _gl_group_data = NULL;
	    contacts_list_destroy(list, true);
	    contacts_query_destroy(query);


}
static bool _vcard_parse_cb(contacts_record_h contact, void *user_data)
{
    if (NULL == contact)
        return false;

    int contact_id = -1;
    int error_code = contacts_db_insert_record(contact, &contact_id);
    if (error_code != CONTACTS_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_db_insert_record() failed: %d", error_code);
        PRINT_MSG("contacts_db_insert_record() failed: %d", error_code);
    }

    contact_ids = eina_list_append(contact_ids, (const void *)contact_id);

    return true;
}




void create_buttons_in_main_window14(appdata_s *ad, Evas_Object *obj, void *event_info){

	Evas_Object *display = _create_new_cd_display(ad, "Managing vCards", _pop_cb,ad->navi2);

	 if (init_ok != 0) {
	        PRINT_MSG("Initialization() failed");
	        return;
	    }

	    contacts_record_h contact;
	    char *vcard_stream = NULL;

	    contacts_record_h contact1 = NULL;
	    contacts_record_h name1 = NULL;
	    contacts_record_create(_contacts_name._uri, &name1);
	    contacts_record_set_str(name1, _contacts_name.first, "Valentine");

	    contacts_record_create(_contacts_contact._uri, &contact1);
	    contacts_record_add_child_record(contact1, _contacts_contact.name, name1);

	    // Add record directly into database:
	    int id;
	    contacts_db_insert_record(contact1, &id);
	    contact_ids = eina_list_append(contact_ids, (const void *)id);
	    contacts_record_destroy(contact1, true);
	    // Making a vCard
	    contacts_db_get_record(_contacts_contact._uri, id, &contact);
	    int error_code = contacts_vcard_make_from_contact(contact, &vcard_stream);
	    if (error_code != CONTACTS_ERROR_NONE) {
	        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_vcard_make_from_contact() failed: %d",
	                   error_code);
	        PRINT_MSG("contacts_vcard_make_from_contact() failed: %d", error_code);
	    }

	    PRINT_MSG("VCARD from contact:");
	    PRINT_MSG("%s", vcard_stream);
	    free(vcard_stream);
	    contacts_record_destroy(contact, true);

	    // Parsing a vCard
	    char vcard_path[BUFLEN];
	    char *path = app_get_resource_path();
	    snprintf(vcard_path, BUFLEN, "%svcard.vcf", path);
	    free(path);
	    error_code = contacts_vcard_parse_to_contact_foreach(vcard_path, _vcard_parse_cb, NULL);
	    if (error_code != CONTACTS_ERROR_NONE)
	        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_vcard_parse_to_contact_foreach() failed: %d",
	                   error_code);
}


typedef struct _contacts_gl_log_data {
    int id;
    char *address;
    int log_type;
    int log_time;
} contacts_gl_log_data_t;

static void _free_gl_log_data(contacts_gl_log_data_t *gl_log_data)
{
    if (NULL == gl_log_data)
        return;

    free(gl_log_data->address);
    free(gl_log_data);
}

static contacts_gl_log_data_t *_create_gl_log_data(contacts_record_h record)
{
    contacts_gl_log_data_t *gl_log_data;

    gl_log_data = malloc(sizeof(contacts_gl_log_data_t));
    memset(gl_log_data, 0x0, sizeof(contacts_gl_log_data_t));

    if (CONTACTS_ERROR_NONE !=
            contacts_record_get_int(record, _contacts_phone_log.id, &gl_log_data->id)) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_record_get_int() failed ");
        _free_gl_log_data(gl_log_data);
        return NULL;
    }

    if (CONTACTS_ERROR_NONE !=
            contacts_record_get_str(record, _contacts_phone_log.address, &gl_log_data->address)) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_record_get_str() failed ");
        _free_gl_log_data(gl_log_data);
        return NULL;
    }

    if (CONTACTS_ERROR_NONE !=
            contacts_record_get_int(record, _contacts_phone_log.log_type, &gl_log_data->log_type)) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_record_get_int() failed ");
        _free_gl_log_data(gl_log_data);
        return NULL;
    }

    if (CONTACTS_ERROR_NONE !=
            contacts_record_get_int(record, _contacts_phone_log.log_time, &gl_log_data->log_time)) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_record_get_int() failed ");
        _free_gl_log_data(gl_log_data);
        return NULL;
    }

    return gl_log_data;
}



void create_buttons_in_main_window15(appdata_s *ad, Evas_Object *obj, void *event_info){

	Evas_Object *display = _create_new_cd_display(ad, "Phone Logs", _pop_cb,ad->navi2);

    int error_code;

    // Creating a Log
    contacts_record_h log;
    error_code = contacts_record_create(_contacts_phone_log._uri, &log);
    if (error_code != CONTACTS_ERROR_NONE)
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_record_create() failed: %d", error_code);

    // Setting Log Properties
    error_code =
        contacts_record_set_int(log, _contacts_phone_log.log_type,
                                CONTACTS_PLOG_TYPE_VOICE_INCOMMING);
    if (error_code != CONTACTS_ERROR_NONE)
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_record_set_int() failed: %d", error_code);

    error_code = contacts_record_set_int(log, _contacts_phone_log.log_time, (int)time(NULL));
    if (error_code != CONTACTS_ERROR_NONE)
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_record_set_int() failed: %d", error_code);

    error_code = contacts_record_set_int(log, _contacts_phone_log.extra_data1, 37);
    if (error_code != CONTACTS_ERROR_NONE)
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_record_set_int() failed: %d", error_code);

    error_code = contacts_record_set_str(log, _contacts_phone_log.address, "+8231-1234-5678");
    if (error_code != CONTACTS_ERROR_NONE)
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_record_set_str() failed: %d", error_code);

    // Inserting a Log to the Database
    int added_log_id = -1;

    error_code = contacts_db_insert_record(log, &added_log_id);
    PRINT_MSG("Inserting log");
    if (error_code != CONTACTS_ERROR_NONE)
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_db_insert_record() failed: %d", error_code);
    else
        PRINT_MSG(" inserted log id %d", added_log_id);

    contacts_record_destroy(log, true);

    // Getting Logs
    int log_id = added_log_id;  // Acquire log ID
    error_code = contacts_db_get_record(_contacts_phone_log._uri, log_id, &log);
    if (error_code != CONTACTS_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_db_get_record() failed: %d", error_code);
        PRINT_MSG("contacts_db_get_record() failed: %d", error_code);
    }

    contacts_list_h list = NULL;
    contacts_query_h query = NULL;

    error_code = contacts_query_create(_contacts_phone_log._uri, &query);
    if (error_code != CONTACTS_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_query_create() failed: %d", error_code);
        PRINT_MSG("contacts_query_create() failed: %d", error_code);
    }

    contacts_filter_h filter = NULL;

    error_code = contacts_filter_create(_contacts_phone_log._uri, &filter);
    if (error_code != CONTACTS_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_filter_create() failed: %d", error_code);
        PRINT_MSG("contacts_filter_create() failed: %d", error_code);
    }

    error_code =
        contacts_filter_add_int(filter, _contacts_phone_log.log_type, CONTACTS_MATCH_EQUAL,
                                CONTACTS_PLOG_TYPE_VOICE_INCOMMING);
    if (error_code != CONTACTS_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_filter_add_int() failed: %d", error_code);
        PRINT_MSG("contacts_filter_add_int() failed: %d", error_code);
    }

    error_code = contacts_filter_add_operator(filter, CONTACTS_FILTER_OPERATOR_OR);
    if (error_code != CONTACTS_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_filter_add_operator() failed: %d", error_code);
        PRINT_MSG("contacts_filter_add_operator() failed: %d", error_code);
    }

    error_code =
        contacts_filter_add_int(filter, _contacts_phone_log.log_type, CONTACTS_MATCH_EQUAL,
                                CONTACTS_PLOG_TYPE_VOICE_OUTGOING);
    if (error_code != CONTACTS_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_filter_add_int() failed: %d", error_code);
        PRINT_MSG("contacts_filter_add_int() failed: %d", error_code);
    }

    error_code = contacts_query_set_filter(query, filter);
    if (error_code != CONTACTS_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_query_set_filter() failed: %d", error_code);
        PRINT_MSG("contacts_query_set_filter() failed: %d", error_code);
    }

    error_code = contacts_db_get_records_with_query(query, 0, 0, &list);
    if (error_code != CONTACTS_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_db_get_records_with_query() failed: %d",
                   error_code);
        PRINT_MSG("contacts_db_get_records_with_query() failed: %d", error_code);
    }

    contacts_filter_destroy(filter);
    contacts_query_destroy(query);

    contacts_record_h record;

    PRINT_MSG("");
    PRINT_MSG("List logs:");
    while (contacts_list_get_current_record_p(list, &record) == CONTACTS_ERROR_NONE) {
        int type;
        error_code = contacts_record_get_int(record, _contacts_phone_log.log_type, &type);
        dlog_print(DLOG_DEBUG, LOG_TAG, "log type: %d", type);
        PRINT_MSG(" - log type: %d", type);

        error_code = contacts_list_next(list);
        if (error_code != CONTACTS_ERROR_NONE)
            break;
    }

    contacts_list_destroy(list, true);

    error_code = contacts_db_get_all_records(_contacts_phone_log._uri, 0, 0, &list);
    if (error_code != CONTACTS_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_db_get_all_records() failed: %d", error_code);
        PRINT_MSG("contacts_db_get_all_records() failed: %d", error_code);
    }

    contacts_gl_log_data_t *gl_log_data = NULL;

    PRINT_MSG("");
    PRINT_MSG("List logs again:");
    while (contacts_list_get_current_record_p(list, &record) == CONTACTS_ERROR_NONE) {
        gl_log_data = _create_gl_log_data(record);

        if (!gl_log_data) {
            PRINT_MSG("Create log data failed");
            break;
        }

        PRINT_MSG(" - Adress: %s", gl_log_data->address);
        error_code = contacts_list_next(list);
        if (error_code != CONTACTS_ERROR_NONE)
            break;
    }

    contacts_list_destroy(list, true);

    error_code = contacts_db_delete_record(_contacts_phone_log._uri, log_id);
    if (error_code != CONTACTS_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_db_delete_record() failed: %d", error_code);
        PRINT_MSG("contacts_db_delete_record() failed: %d", error_code);
    }

}


typedef struct _contacts_gl_speeddial_data {
    int speeddial_number;
    char *number;
    char *display_name;
    char *image_thumbnail_path;
} contacts_gl_speeddial_data_t;

static void _free_gl_speeddial_data(contacts_gl_speeddial_data_t *gl_speeddial_data)
{
    if (NULL == gl_speeddial_data)
        return;

    free(gl_speeddial_data->number);
    free(gl_speeddial_data->display_name);
    free(gl_speeddial_data->image_thumbnail_path);
    free(gl_speeddial_data);
}

static contacts_gl_speeddial_data_t *_create_gl_speeddial_data(contacts_record_h record)
{
    contacts_gl_speeddial_data_t *gl_speeddial_data;

    gl_speeddial_data = malloc(sizeof(contacts_gl_speeddial_data_t));
    memset(gl_speeddial_data, 0x0, sizeof(contacts_gl_speeddial_data_t));

    if (CONTACTS_ERROR_NONE !=
            contacts_record_get_int(record, _contacts_speeddial.speeddial_number,
                                    &gl_speeddial_data->speeddial_number)) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_record_get_int() failed ");
        _free_gl_speeddial_data(gl_speeddial_data);
        return NULL;
    }

    if (CONTACTS_ERROR_NONE !=
            contacts_record_get_str(record, _contacts_speeddial.number, &gl_speeddial_data->number)) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_record_get_str() failed ");
        _free_gl_speeddial_data(gl_speeddial_data);
        return NULL;
    }

    if (CONTACTS_ERROR_NONE !=
            contacts_record_get_str(record, _contacts_speeddial.display_name,
                                    &gl_speeddial_data->display_name)) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_record_get_str() failed ");
        _free_gl_speeddial_data(gl_speeddial_data);
        return NULL;
    }

    if (CONTACTS_ERROR_NONE !=
            contacts_record_get_str(record, _contacts_speeddial.image_thumbnail_path,
                                    &gl_speeddial_data->image_thumbnail_path)) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_record_get_str() failed ");
        _free_gl_speeddial_data(gl_speeddial_data);
        return NULL;
    }

    return gl_speeddial_data;
}


void create_buttons_in_main_window16(appdata_s *ad, Evas_Object *obj, void *event_info){

	Evas_Object *display = _create_new_cd_display(ad, "Speed Dials", _pop_cb,ad->navi2);

    int error_code;

    // Creating a Speed Dial
    contacts_record_h speeddial;
    error_code = contacts_record_create(_contacts_speeddial._uri, &speeddial);
    if (error_code != CONTACTS_ERROR_NONE)
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_record_create() failed: %d", error_code);

    // Setting Speed Dial Properties
    int speeddial_number = 1;   // Acquire speed dial number
    error_code =
        contacts_record_set_int(speeddial, _contacts_speeddial.speeddial_number, speeddial_number);
    if (error_code != CONTACTS_ERROR_NONE)
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_record_set_int() failed: %d", error_code);

    // Creating a contact to assign speed dial to
    contacts_record_h contact = NULL;

    error_code = contacts_record_create(_contacts_contact._uri, &contact);
    if (error_code != CONTACTS_ERROR_NONE)
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_record_create() failed: %d", error_code);

    contacts_record_h name;

    error_code = contacts_record_create(_contacts_name._uri, &name);
    if (error_code != CONTACTS_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_record_create() failed: %d", error_code);
        PRINT_MSG("contacts_record_create() failed: %d", error_code);
    }

    error_code = contacts_record_set_str(name, _contacts_name.first, "John");
    if (error_code != CONTACTS_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_record_set_str() failed: %d", error_code);
        PRINT_MSG("contacts_record_set_str() failed: %d", error_code);
    }

    error_code = contacts_record_set_str(name, _contacts_name.last, "Smith");
    if (error_code != CONTACTS_ERROR_NONE)
        PRINT_MSG("contacts_record_set_str() failed: %d", error_code);

    error_code = contacts_record_add_child_record(contact, _contacts_contact.name, name);
    if (error_code != CONTACTS_ERROR_NONE)
        PRINT_MSG("contacts_record_add_child_record() failed: %d", error_code);

    contacts_record_h number;
    error_code = contacts_record_create(_contacts_number._uri, &number);
    if (error_code != CONTACTS_ERROR_NONE)
        PRINT_MSG("contacts_record_create() failed: %d", error_code);

    error_code = contacts_record_set_str(number, _contacts_number.number, "123456789");
    if (error_code != CONTACTS_ERROR_NONE)
        PRINT_MSG("contacts_record_set_str() failed: %d", error_code);

    error_code = contacts_record_add_child_record(contact, _contacts_contact.number, number);
    if (error_code != CONTACTS_ERROR_NONE)
        PRINT_MSG("contacts_record_add_child_record() failed: %d", error_code);

    int contact_id = -1;
    error_code = contacts_db_insert_record(contact, &contact_id);
    if (error_code != CONTACTS_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_db_insert_record() failed: %d", error_code);
        PRINT_MSG("contacts_db_insert_record() failed: %d", error_code);
    }

    contact_ids = eina_list_append(contact_ids, (const void *)contact_id);

    error_code = contacts_record_destroy(contact, true);
    if (error_code != CONTACTS_ERROR_NONE)
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_record_destroy() failed: %d", error_code);

    contacts_record_h number_record;
    error_code = contacts_db_get_record(_contacts_contact._uri, contact_id, &contact);
    if (error_code != CONTACTS_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_db_get_record() failed: %d", error_code);
        PRINT_MSG("contacts_db_get_record() failed: %d", error_code);
    }

    error_code =
        contacts_record_get_child_record_at_p(contact, _contacts_contact.number, 0, &number_record);
    if (error_code != CONTACTS_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_record_get_child_record_at_p() failed: %d",
                   error_code);
        PRINT_MSG("contacts_record_get_child_record_at_p() failed: %d", error_code);
    }

    int number_id = -1;
    error_code = contacts_record_get_int(number_record, _contacts_number.id, &number_id);
    if (error_code != CONTACTS_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_record_get_int() failed: %d", error_code);
        PRINT_MSG("contacts_record_get_int() failed: %d", error_code);
    }

    error_code = contacts_record_destroy(contact, true);
    if (error_code != CONTACTS_ERROR_NONE)
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_record_destroy() failed: %d", error_code);

    error_code = contacts_record_set_int(speeddial, _contacts_speeddial.number_id, number_id);
    if (error_code != CONTACTS_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_record_set_int() failed: %d", error_code);
        PRINT_MSG("contacts_record_set_int() failed: %d", error_code);
    }

    // Inserting a Speed Dial to the Database
    int added_speeddial_id = -1;
    error_code = contacts_db_insert_record(speeddial, &added_speeddial_id);
    if (error_code != CONTACTS_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_db_insert_record() failed: %d", error_code);
        PRINT_MSG("contacts_db_insert_record() failed: %d", error_code);
    }

    error_code = contacts_record_destroy(speeddial, true);
    if (error_code != CONTACTS_ERROR_NONE)
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_record_destroy() failed: %d", error_code);

    PRINT_MSG("Inserted speed dial id: %d", added_speeddial_id);
    PRINT_MSG("");
    PRINT_MSG("Finding speed dials using a filter");

    // Getting Speed Dials
    int speeddial_id = added_speeddial_id;  // Acquire speed dial ID
    error_code = contacts_db_get_record(_contacts_speeddial._uri, speeddial_id, &speeddial);
    if (error_code != CONTACTS_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_db_get_record() failed: %d", error_code);
        PRINT_MSG("contacts_db_get_record() failed: %d", error_code);
    }

    error_code = contacts_record_destroy(speeddial, true);
    if (error_code != CONTACTS_ERROR_NONE)
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_record_destroy() failed: %d", error_code);

    contacts_list_h list = NULL;
    contacts_query_h query = NULL;

    error_code = contacts_query_create(_contacts_speeddial._uri, &query);
    if (error_code != CONTACTS_ERROR_NONE)
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_query_create() failed: %d", error_code);

    contacts_filter_h filter = NULL;

    error_code = contacts_filter_create(_contacts_speeddial._uri, &filter);
    if (error_code != CONTACTS_ERROR_NONE)
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_filter_create() failed: %d", error_code);

    error_code =
        contacts_filter_add_int(filter, _contacts_speeddial.speeddial_number,
                                CONTACTS_MATCH_LESS_THAN, 3);
    if (error_code != CONTACTS_ERROR_NONE)
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_filter_add_int() failed: %d", error_code);

    error_code = contacts_filter_add_operator(filter, CONTACTS_FILTER_OPERATOR_OR);
    if (error_code != CONTACTS_ERROR_NONE)
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_filter_add_operator() failed: %d", error_code);

    error_code =
        contacts_filter_add_int(filter, _contacts_speeddial.speeddial_number,
                                CONTACTS_MATCH_GREATER_THAN, 15);
    if (error_code != CONTACTS_ERROR_NONE)
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_filter_add_int() failed: %d", error_code);

    error_code = contacts_query_set_filter(query, filter);
    if (error_code != CONTACTS_ERROR_NONE)
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_query_set_filter() failed: %d", error_code);

    error_code = contacts_db_get_records_with_query(query, 0, 0, &list);
    if (error_code != CONTACTS_ERROR_NONE)
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_db_get_records_with_query() failed: %d",
                   error_code);

    error_code = contacts_filter_destroy(filter);
    if (error_code != CONTACTS_ERROR_NONE)
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_filter_destroy() failed: %d", error_code);

    error_code = contacts_query_destroy(query);
    if (error_code != CONTACTS_ERROR_NONE)
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_query_destroy() failed: %d", error_code);

    contacts_record_h record;

    PRINT_MSG("");
    PRINT_MSG("List speed dials:");
    while (contacts_list_get_current_record_p(list, &record) == CONTACTS_ERROR_NONE) {
        int number;
        error_code = contacts_record_get_int(record, _contacts_speeddial.speeddial_number, &number);
        if (error_code != CONTACTS_ERROR_NONE)
            dlog_print(DLOG_ERROR, LOG_TAG, "contacts_record_get_int() failed: %d", error_code);

        dlog_print(DLOG_DEBUG, LOG_TAG, "Speed dial number: %d", number);

        PRINT_MSG(" - Speed dial number: %d", number);

        error_code = contacts_record_get_int(record, _contacts_speeddial.number_id, &number_id);
        if (error_code != CONTACTS_ERROR_NONE)
            dlog_print(DLOG_ERROR, LOG_TAG, "contacts_record_get_int() failed: %d", error_code);

        error_code = contacts_list_next(list);
        if (error_code != CONTACTS_ERROR_NO_DATA)
            break;
        else if (error_code != CONTACTS_ERROR_NONE)
            dlog_print(DLOG_ERROR, LOG_TAG, "contacts_list_next() failed: %d", error_code);
    }

    error_code = contacts_list_destroy(list, true);
    if (error_code != CONTACTS_ERROR_NONE)
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_list_destroy() failed: %d", error_code);

    error_code = contacts_db_get_all_records(_contacts_speeddial._uri, 0, 0, &list);
    if (error_code != CONTACTS_ERROR_NONE)
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_record_get_int() failed: %d", error_code);

    contacts_gl_speeddial_data_t *gl_speeddial_data = NULL;

    while (contacts_list_get_current_record_p(list, &record) == CONTACTS_ERROR_NONE) {
        gl_speeddial_data = _create_gl_speeddial_data(record);

        error_code = contacts_list_next(list);
        _free_gl_speeddial_data(gl_speeddial_data);
        if (error_code != CONTACTS_ERROR_NO_DATA)
            break;
        else if (error_code != CONTACTS_ERROR_NONE)
            dlog_print(DLOG_ERROR, LOG_TAG, "contacts_list_next() failed: %d", error_code);
    }

    error_code = contacts_list_destroy(list, true);
    if (error_code != CONTACTS_ERROR_NONE)
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_list_destroy() failed: %d", error_code);
    PRINT_MSG("");

    // Updating a Speed Dial
    PRINT_MSG("Updating the added speed dial");
    error_code = contacts_db_get_record(_contacts_speeddial._uri, speeddial_number, &speeddial);
    if (error_code != CONTACTS_ERROR_NONE)
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts db get record failed: %d", error_code);

    error_code = contacts_record_set_int(speeddial, _contacts_speeddial.number_id, number_id);
    if (error_code != CONTACTS_ERROR_NONE)
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_record_set_int() failed: %d", error_code);

    error_code = contacts_db_update_record(speeddial);
    if (error_code != CONTACTS_ERROR_NONE)
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_db_update_record() failed: %d", error_code);

    error_code = contacts_record_destroy(speeddial, true);
    if (error_code != CONTACTS_ERROR_NONE)
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_record_destroy() failed: %d", error_code);
    PRINT_MSG("");

    // Removing a Speed Dial
    PRINT_MSG("Removing the added speed dial");
    error_code = contacts_db_get_record(_contacts_speeddial._uri, speeddial_number, &speeddial);
    if (error_code != CONTACTS_ERROR_NONE)
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts db get record failed: %d", error_code);

    error_code = contacts_db_delete_record(_contacts_speeddial._uri, speeddial_number);
    if (error_code != CONTACTS_ERROR_NONE)
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_db_delete_record() failed: %d", error_code);

    error_code = contacts_record_destroy(speeddial, true);
    if (error_code != CONTACTS_ERROR_NONE)
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_record_destroy() failed: %d", error_code);

}


void create_buttons_in_main_window17(appdata_s *ad, Evas_Object *obj, void *event_info){

	Evas_Object *display = _create_new_cd_display(ad, "Managing Sim Contacts", _pop_cb,ad->navi2);

	   bool completed = false;
	    contacts_sim_get_initialization_status(&completed);
	    dlog_print(DLOG_DEBUG, LOG_TAG, "SIM initialization %scompleted", completed ? "" : "not ");
	    PRINT_MSG("SIM initialization %scompleted", completed ? "" : "not ");

	    if (completed) {
	        PRINT_MSG("Import contacts from sim");
	        contacts_sim_import_all_contacts();
	    }
}


void create_buttons_in_main_window18(appdata_s *ad, Evas_Object *obj, void *event_info){

	Evas_Object *display = _create_new_cd_display(ad, "Managing Contacts Setting", _pop_cb,ad->navi2);

    contacts_name_display_order_e display_order;
    int error_code = contacts_setting_get_name_display_order(&display_order);
    if (error_code != CONTACTS_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_setting_get_name_display_order failed: %d",
                   error_code);
        PRINT_MSG("contacts_setting_get_name_display_order failed: %d", error_code);
    }


    // Now you have the display order
    PRINT_MSG("Display order: %s",
              display_order ==
              CONTACTS_NAME_DISPLAY_ORDER_FIRSTLAST ? "CONTACTS_NAME_DISPLAY_ORDER_FIRSTLAST" :
              "CONTACTS_NAME_DISPLAY_ORDER_LASTFIRST");

    contacts_name_sorting_order_e sorting_order;
    error_code = contacts_setting_get_name_sorting_order(&sorting_order);
    if (error_code != CONTACTS_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_setting_get_name_sorting_order failed: %d",
                   error_code);
        PRINT_MSG("contacts_setting_get_name_sorting_order failed: %d", error_code);
    }

    // Now you have the sorting order
    PRINT_MSG("Sorting order: %s",
              sorting_order ==
              CONTACTS_NAME_SORTING_ORDER_FIRSTLAST ? "CONTACTS_NAME_SORTING_ORDER_FIRSTLAST" :
              "CONTACTS_NAME_SORTING_ORDER_LASTFIRST");

    PRINT_MSG("Changing order.");
    error_code = contacts_setting_set_name_display_order(CONTACTS_NAME_DISPLAY_ORDER_FIRSTLAST);
    if (error_code != CONTACTS_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_setting_set_name_display_order failed: %d",
                   error_code);
        PRINT_MSG("contacts_setting_set_name_display_order failed: %d", error_code);
    }

    error_code = contacts_setting_set_name_sorting_order(CONTACTS_NAME_SORTING_ORDER_FIRSTLAST);
    if (error_code != CONTACTS_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_setting_set_name_sorting_order failed: %d",
                   error_code);
        PRINT_MSG("contacts_setting_set_name_sorting_order failed: %d", error_code);
    }
   return;
}

void contacts_deinit(void *data)
{
    int error_code;
    // Here program removes all created contacts
    const Eina_List *l;
    void *element_id = NULL;
    // Deleting a Contact
    EINA_LIST_FOREACH(contact_ids, l, element_id) {
        error_code = contacts_db_delete_record(_contacts_contact._uri, (int)element_id);
        if (error_code != CONTACTS_ERROR_NONE) {
            dlog_print(DLOG_ERROR, LOG_TAG, "contacts_db_delete_record() failed: %d", error_code);
            PRINT_MSG("contacts_db_delete_record() failed: %d", error_code);
        }
    }
    // Deleting a Group
    EINA_LIST_FOREACH(group_ids, l, element_id) {
        error_code = contacts_db_delete_record(_contacts_group._uri, (int)element_id);
        if (error_code != CONTACTS_ERROR_NONE) {
            dlog_print(DLOG_ERROR, LOG_TAG, "contacts_db_delete_record() failed: %d", error_code);
            PRINT_MSG("contacts_db_delete_record() failed: %d", error_code);
        }
    }
    _free_gl_group_data(_gl_group_data);
    eina_list_free(contact_ids);
    eina_list_free(group_ids);

    contacts_db_delete_record(_contacts_speeddial._uri, 3);

    contacts_setting_remove_name_display_order_changed_cb(NULL, NULL);
    contacts_setting_remove_name_sorting_order_changed_cb(NULL, NULL);

    error_code = contacts_disconnect();
    if (error_code != CONTACTS_ERROR_NONE)
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_disconnect() failed: %d", error_code);
}

Eina_Bool _pop_cb(void *data, Elm_Object_Item *item)
{
    elm_win_lower(((appdata_s *)data)->win);

    int error_code =
        contacts_db_remove_changed_cb(_contacts_person._uri, _person_changed_callback, NULL);
    if (error_code != CONTACTS_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_db_remove_changed_cb() failed: %d", error_code);
    }

    // Stop Monitoring Group Changes
    error_code = contacts_db_remove_changed_cb(_contacts_group._uri, _group_changed_callback, NULL);
    if (error_code != CONTACTS_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "contacts_db_remove_changed_cb() failed: %d", error_code);
        PRINT_MSG("contacts_db_remove_changed_cb() failed: %d", error_code);
    }

    return EINA_FALSE;
}


//BLUETOOTH CODES


/* Enable or disable bluetooth */
int bt_onoff_operation(void)
{
    int ret = 0;
    app_control_h service = NULL;
    app_control_create(&service);

    if (service == NULL) {
        dlog_print(DLOG_DEBUG, LOG_TAG, "service_create failed!\n");
        return -1;
    }

    app_control_set_operation(service, "http://tizen.org/appcontrol/operation/edit");
    app_control_set_mime(service, "application/x-bluetooth-on-off");
    ret = app_control_send_launch_request(service, NULL, NULL);

    app_control_destroy(service);
    if (ret == APP_CONTROL_ERROR_NONE) {
        dlog_print(DLOG_DEBUG, LOG_TAG, "Succeeded to Bluetooth On/Off app!\n");
        return 0;
    } else {
        dlog_print(DLOG_DEBUG, LOG_TAG, "Failed to relaunch Bluetooth On/Off app!\n");
        return -1;
    }
}

/* Called when the state of device discovery changes */
void adapter_device_discovery_state_changed_cb(int result,
        bt_adapter_device_discovery_state_e discovery_state,
        bt_adapter_device_discovery_info_s *discovery_info,
        void *user_data)
{
    if (result != BT_ERROR_NONE) {
        PRINT_MSG("[adapter_device_discovery_state_changed_cb] Failed! result(%d).", result);
        dlog_print(DLOG_ERROR, LOG_TAG,
                   "[adapter_device_discovery_state_changed_cb] Failed! result(%d).", result);
        return;
    }

    GList **searched_device_list = (GList **)user_data;

    switch (discovery_state) {
    case BT_ADAPTER_DEVICE_DISCOVERY_STARTED:
        PRINT_MSG("BT_ADAPTER_DEVICE_DISCOVERY_STARTED");
        dlog_print(DLOG_DEBUG, LOG_TAG, "BT_ADAPTER_DEVICE_DISCOVERY_STARTED");
        break;

    case BT_ADAPTER_DEVICE_DISCOVERY_FINISHED:
        PRINT_MSG("BT_ADAPTER_DEVICE_DISCOVERY_FINISHED");
        dlog_print(DLOG_DEBUG, LOG_TAG, "BT_ADAPTER_DEVICE_DISCOVERY_FINISHED");
        break;

    case BT_ADAPTER_DEVICE_DISCOVERY_FOUND:
        PRINT_MSG("BT_ADAPTER_DEVICE_DISCOVERY_FOUND");
        dlog_print(DLOG_DEBUG, LOG_TAG, "BT_ADAPTER_DEVICE_DISCOVERY_FOUND");

        if (discovery_info != NULL) {
            PRINT_MSG("Device Address: %s", discovery_info->remote_address);
            dlog_print(DLOG_DEBUG, LOG_TAG, "Device Address: %s", discovery_info->remote_address);
            PRINT_MSG("Device Name is: %s", discovery_info->remote_name);
            dlog_print(DLOG_DEBUG, LOG_TAG, "Device Name: %s", discovery_info->remote_name);
            bt_adapter_device_discovery_info_s *new_device_info =
                malloc(sizeof(bt_adapter_device_discovery_info_s));

            if (new_device_info != NULL) {
                memcpy(new_device_info, discovery_info, sizeof(bt_adapter_device_discovery_info_s));
                new_device_info->remote_address = strdup(discovery_info->remote_address);
                new_device_info->remote_name = strdup(discovery_info->remote_name);
                *searched_device_list =
                    g_list_append(*searched_device_list, (gpointer) new_device_info);
            }
        }

        break;
    }
}

/* Called when the mode  of device discovery changes */
void adapter_visibility_mode_changed_cb(int result, bt_adapter_visibility_mode_e visibility_mode,
                                        void *user_data)
{
    if (result != BT_ERROR_NONE) {
        // Error handling
        return;
    }

    if (visibility_mode == BT_ADAPTER_VISIBILITY_MODE_NON_DISCOVERABLE) {
        PRINT_MSG("[visibility_mode_changed_cb] None discoverable mode!");
        dlog_print(DLOG_DEBUG, LOG_TAG, "[visibility_mode_changed_cb] None discoverable mode!");
    } else if (visibility_mode == BT_ADAPTER_VISIBILITY_MODE_GENERAL_DISCOVERABLE) {
        PRINT_MSG("[visibility_mode_changed_cb] General discoverable mode!");
        dlog_print(DLOG_DEBUG, LOG_TAG, "[visibility_mode_changed_cb] General discoverable mode!");
    } else {
        PRINT_MSG("[visibility_mode_changed_cb] Limited discoverable mode!");
        dlog_print(DLOG_DEBUG, LOG_TAG, "[visibility_mode_changed_cb] Limited discoverable mode!");
    }
}

char *remote_server_address = NULL; // Server address for connecting

/* Called when you get bonded devices repeatedly */
bool adapter_bonded_device_cb(bt_device_info_s *device_info, void *user_data)
{
    if (device_info == NULL)
        return true;

    if (!strcmp(device_info->remote_name, (char *)user_data)) {
        PRINT_MSG("The server device is found in bonded device list. Address(%s)",
                  device_info->remote_address);
        dlog_print(DLOG_DEBUG, LOG_TAG,
                   "The server device is found in bonded device list. Address(%s)",
                   device_info->remote_address);
        remote_server_address = strdup(device_info->remote_address);
        // If you want to stop iterating, you can return "false"
    }

    /* Get information about bonded device */
    static int count_of_bonded_device = 1;
    PRINT_MSG("Get information about the bonded device(%d)", count_of_bonded_device);
    PRINT_MSG("remote address = %s.", device_info->remote_address);
    PRINT_MSG("remote name = %s.", device_info->remote_name);
    PRINT_MSG("service count = %d.", device_info->service_count);
    PRINT_MSG("bonded %s.", device_info->is_bonded ? "true" : "false");
    PRINT_MSG("connected %s.", device_info->is_connected ? "true" : "false");
    PRINT_MSG("authorized %s.", device_info->is_authorized ? "true" : "false");

    dlog_print(DLOG_DEBUG, LOG_TAG, "Get information about the bonded device(%d)",
               count_of_bonded_device);
    dlog_print(DLOG_DEBUG, LOG_TAG, "remote address = %s.", device_info->remote_address);
    dlog_print(DLOG_DEBUG, LOG_TAG, "remote name = %s.", device_info->remote_name);
    dlog_print(DLOG_DEBUG, LOG_TAG, "service count = %d.", device_info->service_count);
    dlog_print(DLOG_DEBUG, LOG_TAG, "bonded %s.", device_info->is_bonded ? "true" : "false");
    dlog_print(DLOG_DEBUG, LOG_TAG, "connected %s.", device_info->is_connected ? "true" : "false");
    dlog_print(DLOG_DEBUG, LOG_TAG, "authorized %s.",
               device_info->is_authorized ? "true" : "false");

    elm_entry_entry_set(ap_name_entry, device_info->remote_address);

    PRINT_MSG("major_device_class %d.", device_info->bt_class.major_device_class);
    PRINT_MSG("minor_device_class %d.", device_info->bt_class.minor_device_class);
    PRINT_MSG("major_service_class_mask %d.", device_info->bt_class.major_service_class_mask);

    dlog_print(DLOG_DEBUG, LOG_TAG, "major_device_class %d.",
               device_info->bt_class.major_device_class);
    dlog_print(DLOG_DEBUG, LOG_TAG, "minor_device_class %d.",
               device_info->bt_class.minor_device_class);
    dlog_print(DLOG_DEBUG, LOG_TAG, "major_service_class_mask %d.",
               device_info->bt_class.major_service_class_mask);
    count_of_bonded_device++;   // Keep iterating

    return true;
}

/* Called when the process of creating bond finishes */
void device_bond_created_cb(int result, bt_device_info_s *device_info, void *user_data)
{
    if (result != BT_ERROR_NONE) {
        PRINT_MSG("[bt_device_bond_created_cb] Failed. result(%d).", result);
        dlog_print(DLOG_ERROR, LOG_TAG, "[bt_device_bond_created_cb] Failed. result(%d).", result);
        return;
    }

    if (remote_server_address != NULL && device_info != NULL
            && !strcmp(device_info->remote_address, remote_server_address)) {
        PRINT_MSG("Callback: A bond with chat_server is created.");
        PRINT_MSG("Callback: The number of service - %d.", device_info->service_count);
        dlog_print(DLOG_DEBUG, LOG_TAG, "Callback: A bond with chat_server is created.");
        dlog_print(DLOG_DEBUG, LOG_TAG, "Callback: The number of service - %d.",
                   device_info->service_count);
        PRINT_MSG("Callback: is_bonded - %d.", device_info->is_bonded);
        PRINT_MSG("Callback: is_connected - %d.", device_info->is_connected);
        dlog_print(DLOG_DEBUG, LOG_TAG, "Callback: is_bonded - %d.", device_info->is_bonded);
        dlog_print(DLOG_DEBUG, LOG_TAG, "Callback: is_connected - %d.", device_info->is_connected);
    } else {
        PRINT_MSG("Callback: A bond with another device is created.");
        dlog_print(DLOG_ERROR, LOG_TAG, "Callback: A bond with another device is created.");
    }
}
void _bluetooth_page(appdata_s *ad)
{
    // Setting the window
    ad->win = elm_win_util_standard_add(PACKAGE, PACKAGE);
    elm_win_conformant_set(ad->win, EINA_TRUE);
    elm_win_autodel_set(ad->win, EINA_TRUE);
    elm_win_indicator_mode_set(ad->win, ELM_WIN_INDICATOR_SHOW);
    elm_win_indicator_opacity_set(ad->win, ELM_WIN_INDICATOR_OPAQUE);



		Evas_Object *conform = elm_conformant_add(ad->win);

	    evas_object_size_hint_weight_set(conform, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
	    elm_win_resize_object_add(ad->win, conform);
	    evas_object_show(conform);

	// Create a naviframe
	    ad->navi1 = elm_naviframe_add(conform);
	    evas_object_size_hint_align_set(ad->navi1, EVAS_HINT_FILL, EVAS_HINT_FILL);
	    evas_object_size_hint_weight_set(ad->navi1, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);

	    elm_object_content_set(conform, ad->navi1);
	    evas_object_show(ad->navi1);

	    // Fill the list with items
	    create_buttons_in_main_window1(ad);

	    eext_object_event_callback_add(ad->navi1, EEXT_CALLBACK_BACK, eext_naviframe_back_cb, NULL);

	    // Show the window after base gui is set up
	    evas_object_show(ad->win);


}



/* Bluetooth visibility setting application to change visibility mode */
int bt_set_visibility_operation(void *data, Evas_Object *obj, void *event_info)
{
    int ret = 0;
    app_control_h service = NULL;
    app_control_create(&service);

    if (service == NULL) {
        dlog_print(DLOG_DEBUG, LOG_TAG, "service_create failed!\n");
        return 0;
    }

    app_control_set_operation(service, "http://tizen.org/appcontrol/operation/edit");
    app_control_set_mime(service, "application/x-bluetooth-visibility");
    ret = app_control_send_launch_request(service, NULL, NULL);

    app_control_destroy(service);
    if (ret == APP_CONTROL_ERROR_NONE) {
        dlog_print(DLOG_DEBUG, LOG_TAG, "Succeeded to Bluetooth On/Off app!\n");
        return 0;
    } else {
        dlog_print(DLOG_DEBUG, LOG_TAG, "Failed to relaunch Bluetooth On/Off app!\n");
        return -1;
    }
}

/* Finding other devices */
void _bluetooth_finding_devices_cb(appdata_s *ad, Evas_Object *obj, void *event_info)
{
	// Setting the window
		    ad->win = elm_win_util_standard_add(PACKAGE, PACKAGE);
		    elm_win_conformant_set(ad->win, EINA_TRUE);
		    elm_win_autodel_set(ad->win, EINA_TRUE);
		    elm_win_indicator_mode_set(ad->win, ELM_WIN_INDICATOR_SHOW);
		    elm_win_indicator_opacity_set(ad->win, ELM_WIN_INDICATOR_OPAQUE);



				Evas_Object *conform = elm_conformant_add(ad->win);

			    evas_object_size_hint_weight_set(conform, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
			    elm_win_resize_object_add(ad->win, conform);
			    evas_object_show(conform);

			// Create a naviframe
			    ad->navi2 = elm_naviframe_add(conform);
			    evas_object_size_hint_align_set(ad->navi2, EVAS_HINT_FILL, EVAS_HINT_FILL);
			    evas_object_size_hint_weight_set(ad->navi2, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);

			    elm_object_content_set(conform, ad->navi2);
			    evas_object_show(ad->navi2);

			    // Fill the list with items
			    create_buttons_in_main_window11(ad,obj,event_info);


			    eext_object_event_callback_add(ad->navi2, EEXT_CALLBACK_BACK, eext_naviframe_back_cb, NULL);

			    // Show the window after base gui is set up
			    evas_object_show(ad->win);

}



void create_buttons_in_main_window11(appdata_s *ad, Evas_Object *obj, void *event_info)
{

	Evas_Object *display = _create_new_cd_display(ad, "", _pop_cb,ad->navi2);

    /* Start discovery devices */
    int ret = bt_adapter_start_device_discovery();
    if (ret != BT_ERROR_NONE) {
        PRINT_MSG("Start device discovery failed.");
        dlog_print(DLOG_ERROR, LOG_TAG, "[bt_adapter_start_discover_device] Failed.");
    }

    /* Check the current visibility of your device */
    bt_adapter_visibility_mode_e mode;
    int duration = 1;
    bt_adapter_get_visibility(&mode, &duration);

    if (mode == BT_ADAPTER_VISIBILITY_MODE_NON_DISCOVERABLE) {
        PRINT_MSG("The device is not discoverable.");
        dlog_print(DLOG_DEBUG, LOG_TAG, "The device is not discoverable.");
    } else if (mode == BT_ADAPTER_VISIBILITY_MODE_GENERAL_DISCOVERABLE) {
        PRINT_MSG("The device is discoverable. No time limit.");
        dlog_print(DLOG_DEBUG, LOG_TAG, "The device is discoverable. No time limit.");
    } else {
        PRINT_MSG("The device is discoverable for a set period of time.");
        dlog_print(DLOG_DEBUG, LOG_TAG, "The device is discoverable for a set period of time.");
    }

    /* Register the visibility change callback */
    ret = bt_adapter_set_visibility_mode_changed_cb(adapter_visibility_mode_changed_cb, NULL);
    if (ret != BT_ERROR_NONE) {
        PRINT_MSG("[bt_adapter_set_visibility_mode_changed_cb] Failed.");
        dlog_print(DLOG_ERROR, LOG_TAG, "[bt_adapter_set_visibility_mode_changed_cb] Failed.");
    }

    char *remote_server_name = "server device";

    /* Querying bonded devices */
    ret = bt_adapter_foreach_bonded_device(adapter_bonded_device_cb, remote_server_name);
    if (ret != BT_ERROR_NONE) {
        PRINT_MSG("[bt_adapter_foreach_bonded_device] Failed!");
        dlog_print(DLOG_ERROR, LOG_TAG, "[bt_adapter_foreach_bonded_device] Failed!");
        free(remote_server_address);
        return;
    }

    /* Get notified when the bonding has finished */
    ret = bt_device_set_bond_created_cb(device_bond_created_cb, NULL);
    if (ret != BT_ERROR_NONE) {
        PRINT_MSG("[bt_device_set_bond_created_cb] Failed.");
        dlog_print(DLOG_ERROR, LOG_TAG, "[bt_device_set_bond_created_cb] failed.");
        free(remote_server_address);
        return;
    }
}

static void _demo_cb(appdata_s *ad)
{
	dlog_print(DLOG_ERROR, LOG_TAG, "Demo");
}


void create_buttons_in_main_window4(appdata_s *ad, void *data, Evas_Object *obj, void *event_info){

	Evas_Object *display = _create_new_cd_display(ad, "Bonding", _pop_cb,ad->navi3);
    const char *address = elm_entry_entry_get(ap_name_entry);

    /* Create bond with device */
    PRINT_MSG("Trying to bond with %s", address);
    int ret = bt_device_create_bond(address);
    if (ret != BT_ERROR_NONE) {
        PRINT_MSG("[bt_device_create_bond] failed.");
        dlog_print(DLOG_ERROR, LOG_TAG, "[bt_device_create_bond] failed.");
    } else {
        PRINT_MSG
        ("[bt_device_create_bond] succeeded. device_bond_created_cb callback will be called.");
        dlog_print(DLOG_DEBUG, LOG_TAG,
                   "[bt_device_create_bond] succeeded. device_bond_created_cb callback will be called.");
    }
}

/* Connecting to Other Devices Using SPP */

const char *my_uuid = "00001101-0000-1000-8000-00805F9B34FB";

/* Called when the socket connection state changes */
void socket_connection_state_changed(int result, bt_socket_connection_state_e connection_state,
                                     bt_socket_connection_s *connection, void *user_data)
{
    if (result != BT_ERROR_NONE) {
        PRINT_MSG("[socket_connection_state_changed_cb] Failed. result = %d.", result);
        dlog_print(DLOG_ERROR, LOG_TAG, "[socket_connection_state_changed_cb] Failed. result = %d.",
                   result);
        return;
    }

    /* Share data between devices after establishing a connection */
    if (connection_state == BT_SOCKET_CONNECTED) {
        PRINT_MSG("Callback: Connected.");

        if (connection != NULL) {
            PRINT_MSG("Callback: Socket of connection - %d.", connection->socket_fd);
            PRINT_MSG("Callback: Role of connection - %d.", connection->local_role);
            PRINT_MSG("Callback: Address of connection - %s.", connection->remote_address);
            dlog_print(DLOG_DEBUG, LOG_TAG, "Callback: Socket of connection - %d.",
                       connection->socket_fd);
            dlog_print(DLOG_DEBUG, LOG_TAG, "Callback: Role of connection - %d.",
                       connection->local_role);
            dlog_print(DLOG_DEBUG, LOG_TAG, "Callback: Address of connection - %s.",
                       connection->remote_address);

            /* Disconnect unused socket */
            if (server_socket_fd != -1)
                bt_socket_disconnect_rfcomm(server_socket_fd);

            /* socket_fd is used for sending data and disconnecting a device */
            server_socket_fd = connection->socket_fd;

            PRINT_MSG("Sending message...");
            char data[] = "Sending test";

            /* Send data */
            int ret = bt_socket_send_data(server_socket_fd, data, sizeof(data));
            if (ret < 0) {
                PRINT_MSG("[bt_socket_send_data] failed.");
                dlog_print(DLOG_ERROR, LOG_TAG, "[bt_socket_send_data] failed.");
                return;
            }
        } else {
            PRINT_MSG("Callback: No connection data");
            dlog_print(DLOG_DEBUG, LOG_TAG, "Callback: No connection data");
        }
    } else {
        PRINT_MSG("Callback: Disconnected.");
        server_socket_fd = -1;
        dlog_print(DLOG_DEBUG, LOG_TAG, "Callback: Disconnected.");

        if (connection != NULL) {
            PRINT_MSG("Callback: Socket of disconnection - %d.", connection->socket_fd);
            PRINT_MSG("Callback: Address of connection - %s.", connection->remote_address);
            dlog_print(DLOG_DEBUG, LOG_TAG, "Callback: Socket of disconnection - %d.",
                       connection->socket_fd);
            dlog_print(DLOG_DEBUG, LOG_TAG, "Callback: Address of connection - %s.",
                       connection->remote_address);
        } else {
            PRINT_MSG("Callback: No connection data");
            dlog_print(DLOG_DEBUG, LOG_TAG, "Callback: No connection data");
        }
    }
}

/* Called when data received */
void _bt_socket_data_received_cb(bt_socket_received_data_s *data, void *user_data)
{
    if (data == NULL) {
        PRINT_MSG("No received data!");
        dlog_print(DLOG_DEBUG, LOG_TAG, "No received data!");
        return;
    }

    PRINT_MSG("Received data");
    PRINT_MSG("  Socket fd: %d", data->socket_fd);
    PRINT_MSG("  Data: %s", data->data);
    PRINT_MSG("  Size: %d", data->data_size);

    char *comma = malloc(100);
    comma = strtok(data->data,

    "|");
    char *comma1 = malloc(100);
    comma1=strtok(NULL,

    "|");
    contact_name = malloc(100);

    contact_number = malloc(100);

    PRINT_MSG(

    "\n\nReceived Contact");
    PRINT_MSG(

    " Name: %s", comma);
    PRINT_MSG(

    " Number: %s", comma1);
    sprintf(contact_name,

    "%s", comma);
    sprintf(contact_number,

    "%s", comma1);



    dlog_print(DLOG_DEBUG, LOG_TAG, "Socket fd: %d", data->socket_fd);
    dlog_print(DLOG_DEBUG, LOG_TAG, "Data: %s", data->data);
    dlog_print(DLOG_DEBUG, LOG_TAG, "Size: %d", data->data_size);



}

/* Called when the connection state changes */
void __bt_gatt_connection_state_changed_cb(int result, bool connected,
        const char *remote_address, void *user_data)
{
    if (connected) {
        dlog_print(DLOG_DEBUG, LOG_TAG, "LE connected");
        PRINT_MSG("LE connected");
    } else {
        dlog_print(DLOG_DEBUG, LOG_TAG, "LE disconnected");
        PRINT_MSG("LE disconnected");
    }
}

/* Called when the characteristic descriptors is discovering */
bool __bt_gatt_client_foreach_cb(int total, int index, bt_gatt_h chr_handle, void *data)
{
    char *uuid = NULL;

    bt_gatt_get_uuid(chr_handle, &uuid);

    // Append obtained uuid to list
    Eina_List **list = (Eina_List **)data;
    *list = eina_list_append(*list, uuid);

    dlog_print(DLOG_DEBUG, LOG_TAG, "\t[%d / %d] uuid : (%s)", index, total, uuid);

    return true;
}

/* Called after the writing operation is complete */

void __bt_gatt_client_write_complete_cb(int result, bt_gatt_h gatt_handle, void *data)
{
    char *uuid = NULL;

    bt_gatt_get_uuid(gatt_handle, &uuid);

    dlog_print(DLOG_DEBUG, LOG_TAG, "Write %s for uuid : (%s)",
               result == BT_ERROR_NONE ? "Success" : "Fail", uuid);
    PRINT_MSG("Write %s for uuid : (%s)", result == BT_ERROR_NONE ? "Success" : "Fail", uuid);

    free(uuid);

    return;
}

/* Called after the reading operation is complete to handle values */
void __bt_gatt_client_read_complete_cb(int result, bt_gatt_h gatt_handle, void *data)
{
    dlog_print(DLOG_DEBUG, LOG_TAG, "Read %s", result == BT_ERROR_NONE ? "succeeded" : "failed");
    PRINT_MSG("Read %s", result == BT_ERROR_NONE ? "succeeded" : "failed");

    /* Obtain the value */
    char *value = NULL;
    int len;
    int ret = bt_gatt_get_value(gatt_handle, &value, &len);
    if (ret != BT_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "bt_gatt_get_value failed : %d", ret);
        PRINT_MSG("bt_gatt_get_value failed : %d", ret);
    } else {
        char *tmp_str = malloc(sizeof(char) * (len + 1));
        strncpy(tmp_str, value, len);
        tmp_str[len] = 0x00;
        PRINT_MSG("Message from service: %s len: %d", tmp_str, len);
        free(value);
        free(tmp_str);
    }
}

/* Called after registering the callback operation to display the changed value */
void __bt_gatt_client_value_changed_cb(bt_gatt_h chr, char *value, int len, void *user_data)
{
    char *uuid = NULL;
    int i;

    bt_gatt_get_uuid(chr, &uuid);

    dlog_print(DLOG_DEBUG, LOG_TAG, "Value changed for [%s] len [%d]", uuid, len);
    PRINT_MSG("Value changed for [%s] len [%d]", uuid, len);

    for (i = 0; i < len; i++) {
        dlog_print(DLOG_DEBUG, LOG_TAG, "value %u", value[i]);
        PRINT_MSG("value %u", value[i]);
    }

    free(uuid);

    int ret = bt_gatt_client_unset_characteristic_value_changed_cb(chr);
    if (ret != BT_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG,
                   "[bt_gatt_client_unset_characteristic_value_changed_cb] failed : %d", ret);
        PRINT_MSG("[bt_gatt_client_unset_characteristic_value_changed_cb] failed : %d", ret);
    }

    return;
}

void use_service(void *data, Evas_Object *obj, void *event_info)
{
    const char *service_uuid = elm_object_item_text_get(event_info);

    Evas_Object *pop_win = (Evas_Object *)evas_object_data_get(obj, "inwin");
    evas_object_hide(pop_win);

    PRINT_MSG("Selected service %s", service_uuid);

    // Use uuids to manipulate device and free the list content
    bt_gatt_h desc = NULL;
    bt_gatt_h svc = NULL;
    bt_gatt_h chr = NULL;

    int ret = bt_gatt_client_get_service(client, service_uuid, &svc);
    if (ret != BT_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "[bt_gatt_client_get_service] failed : %d", ret);
        PRINT_MSG("[bt_gatt_client_get_service] failed : %d", ret);
    }

    /* Discover available characteristics and add their uuids to the list */
    Eina_List *characteristics_uuid_list = NULL;

    ret =
        bt_gatt_service_foreach_characteristics(svc, __bt_gatt_client_foreach_cb,
                &characteristics_uuid_list);
    if (ret != BT_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "bt_gatt_service_foreach_characteristics failed : %d", ret);
        PRINT_MSG("Couldn't list characteristics");
        return;
    }

    if (characteristics_uuid_list == NULL) {
        PRINT_MSG("No characteristic found");
        return;
    }

    // Use uuids of characteristics
    char *characteristic_uuid;
    EINA_LIST_FREE(characteristics_uuid_list, characteristic_uuid) {
        PRINT_MSG("Characteristic (%s)", characteristic_uuid);

        ret = bt_gatt_service_get_characteristic(svc, characteristic_uuid, &chr);
        if (ret == BT_ERROR_NO_DATA) {
            dlog_print(DLOG_ERROR, LOG_TAG, "[bt_gatt_service_get_characteristic] failed : %d",
                       ret);
            PRINT_MSG("The service has no characteristic");
            return;
        } else if (ret != BT_ERROR_NONE) {
            dlog_print(DLOG_ERROR, LOG_TAG, "[bt_gatt_service_get_characteristic] failed : %d",
                       ret);
            PRINT_MSG("[bt_gatt_service_get_characteristic] failed : %d", ret);
            return;
        }

        ret =
            bt_gatt_client_set_characteristic_value_changed_cb(chr,
                    __bt_gatt_client_value_changed_cb,
                    NULL);
        if (BT_ERROR_NOT_SUPPORTED == ret) {
            dlog_print(DLOG_INFO, LOG_TAG,
                       "Monitoring characteristic value changes is not supported.");
        } else if (ret != BT_ERROR_NONE) {
            dlog_print(DLOG_ERROR, LOG_TAG,
                       "[bt_gatt_client_set_characteristic_value_changed_cb] failed : %d", ret);
            PRINT_MSG("[bt_gatt_client_set_characteristic_value_changed_cb] failed : %d", ret);
        }

        /* Discover available descriptors and add their uuids to the list */
        Eina_List *descriptors_uuid_list = NULL;

        ret = bt_gatt_characteristic_foreach_descriptors(chr,
                __bt_gatt_client_foreach_cb,
                &descriptors_uuid_list);
        if (ret != BT_ERROR_NONE)
            dlog_print(DLOG_ERROR, LOG_TAG,
                       "bt_gatt_characteristic_foreach_descriptors failed : %d", ret);

        // Get the properties using the characteristic handle
        int properties;

        // Get the characteristic handle using bt_gatt_service_get_characteristic()
        ret = bt_gatt_characteristic_get_properties(chr, &properties);
        if (ret != BT_ERROR_NONE) {
            dlog_print(DLOG_ERROR, LOG_TAG, "[bt_gatt_characteristic_get_properties] failed : %d",
                       ret);
            PRINT_MSG("[bt_gatt_characteristic_get_properties] failed : %d", ret);
        }

        if ((properties & BT_GATT_PROPERTY_WRITE) == BT_GATT_PROPERTY_WRITE) {  // Is writable
            PRINT_MSG("The service is writable");

            // Write only to the first writable service
            char reset = 0x03;  /* Change to sensible instruction of used device */

            bt_gatt_characteristic_set_write_type(chr, BT_GATT_WRITE_TYPE_WRITE);
            bt_gatt_set_value(chr, &reset, sizeof(reset));
            ret = bt_gatt_client_write_value(chr, __bt_gatt_client_write_complete_cb, NULL);
            if (ret != BT_ERROR_NONE) {
                dlog_print(DLOG_ERROR, LOG_TAG, "[bt_gatt_client_write_value] failed : %d", ret);
                PRINT_MSG("[bt_gatt_client_write_value] failed : %d", ret);
            }
        } else if ((properties & BT_GATT_PROPERTY_READ) == BT_GATT_PROPERTY_READ) { // Is readable
            PRINT_MSG("The service is readable");

            ret = bt_gatt_client_read_value(chr, __bt_gatt_client_read_complete_cb, NULL);
            if (ret != BT_ERROR_NONE) {
                dlog_print(DLOG_ERROR, LOG_TAG, "[bt_gatt_client_read_value] failed : %d", ret);
                PRINT_MSG("[bt_gatt_client_read_value] failed : %d", ret);
            }
        }

        // Use uuids of characteristics
        char *descriptor_uuid;
        EINA_LIST_FREE(descriptors_uuid_list, descriptor_uuid) {
            PRINT_MSG("Descriptor (%s)", descriptor_uuid);

            ret = bt_gatt_characteristic_get_descriptor(chr, descriptor_uuid, &desc);
            if (ret == BT_ERROR_NO_DATA) {
                dlog_print(DLOG_ERROR, LOG_TAG, "[bt_gatt_service_get_characteristic] failed : %d",
                           ret);
                PRINT_MSG("The service has no descriptor");
                free(descriptor_uuid);
                continue;
            } else if (ret != BT_ERROR_NONE) {
                dlog_print(DLOG_ERROR, LOG_TAG,
                           "[bt_gatt_characteristic_get_descriptor] failed : %d", ret);
                PRINT_MSG("[bt_gatt_characteristic_get_descriptor] failed : %d", ret);
            }

            free(descriptor_uuid);
        }

        ret = bt_gatt_client_unset_characteristic_value_changed_cb(chr);
        if (ret != BT_ERROR_NONE) {
            dlog_print(DLOG_ERROR, LOG_TAG,
                       "[bt_gatt_client_unset_characteristic_value_changed_cb] failed : %d", ret);
        }

        free(characteristic_uuid);
    }
}

/* Called when the service characteristics discovery is initiating */
bool __bt_gatt_client_foreach_svc_cb(int total, int index, bt_gatt_h svc_handle, void *data)
{
    char *uuid = NULL;

    bt_gatt_get_uuid(svc_handle, &uuid);

    dlog_print(DLOG_DEBUG, LOG_TAG, "Found service: [%d / %d] uuid : (%s)", index, total, uuid);

    // Append obtained uuid to list
    Evas_Object **list = (Evas_Object **)data;
    elm_list_item_append(*list, uuid, NULL, NULL, use_service, NULL);
    elm_list_go(*list);
    free(uuid);

    return true;
}

/* Connecting as a server */
void _bluetooth_services_init()
{
    /* To establish a connection, create a RFCOMM Bluetooth socket */
    int ret = bt_socket_create_rfcomm(my_uuid, &my_socket_fd);
    if (ret != BT_ERROR_NONE) {
        PRINT_MSG("bt_socket_create_rfcomm() failed.");
        dlog_print(DLOG_ERROR, LOG_TAG, "bt_socket_create_rfcomm() failed.");
        return;
    }

    /* Get notified about which device connects to your device */
    ret = bt_socket_set_connection_state_changed_cb(socket_connection_state_changed, NULL);
    if (ret != BT_ERROR_NONE) {
        PRINT_MSG("[bt_socket_set_connection_state_changed_cb] failed.");
        dlog_print(DLOG_ERROR, LOG_TAG, "[bt_socket_set_connection_state_changed_cb] failed.");
        return;
    }

    /* Listen for an incoming connection */
    ret = bt_socket_listen_and_accept_rfcomm(my_socket_fd, 5);
    if (ret != BT_ERROR_NONE) {
        PRINT_MSG("[bt_socket_listen_and_accept_rfcomm] failed. %d", ret);
        dlog_print(DLOG_ERROR, LOG_TAG, "[bt_socket_listen_and_accept_rfcomm] failed.");
        return;
    }

    /* Read data from other devices */
    ret = bt_socket_set_data_received_cb(_bt_socket_data_received_cb, NULL);
    if (ret != BT_ERROR_NONE) {
        PRINT_MSG("[bt_socket_set_data_received_cb] failed.");
        dlog_print(DLOG_ERROR, LOG_TAG, "[bt_socket_set_data_received_cb] failed.");
        return;
    }

    /* Register a callback for connection state changes */
    ret = bt_gatt_set_connection_state_changed_cb(__bt_gatt_connection_state_changed_cb, NULL);
    if (ret != BT_ERROR_NONE) {
        PRINT_MSG("[bt_gatt_set_connection_state_changed_cb] failed.");
        dlog_print(DLOG_ERROR, LOG_TAG, "[bt_gatt_set_connection_state_changed_cb] failed.");
        return;
    }
}


void create_buttons_in_main_window5(appdata_s *ad, Evas_Object *obj, void *event_info){

	Evas_Object *display = _create_new_cd_display(ad, "Serial Port Profile ", _pop_cb,ad->navi4);

    const char *service_uuid = "00001101-0000-1000-8000-00805F9B34FB";

    elm_object_text_set(obj, "Send message through socket");

    /* Send message if already connected to the server */
    if (server_socket_fd != -1) {
    	//MODIFIED
    	char data[100];

    	const char *data_msg = elm_entry_entry_get(msg_entry); //Getting text from newly added text box

    	PRINT_MSG("Sending message... [%s]", data_msg);

    	sprintf(data, "%s", data_msg);

    	PRINT_MSG("Msg... [%s]", data);


        /* Send data */
        int ret = bt_socket_send_data(server_socket_fd, data, sizeof(data));
        if (ret < 0) {
            PRINT_MSG("[bt_socket_send_data] failed.");
            dlog_print(DLOG_ERROR, LOG_TAG, "[bt_socket_send_data] failed.");
        } else
            return;
    } else {
        const char *remote_server_address = elm_entry_entry_get(ap_name_entry);

        if (!strcmp(remote_server_address, "")) {
            PRINT_MSG
            ("Enter other device MAC address. This device needs to have this application launched.");
            return;
        }

        /* Request a connection to the Bluetooth server */
        PRINT_MSG("Connecting to the server...");
        int ret = bt_socket_connect_rfcomm(remote_server_address, service_uuid);
        if (ret != BT_ERROR_NONE) {
            PRINT_MSG("[bt_socket_connect_rfcomm] failed.");
            dlog_print(DLOG_ERROR, LOG_TAG, "[bt_socket_connect_rfcomm] failed.");
            return;
        } else {
            PRINT_MSG
            ("[bt_socket_connect_rfcomm] Succeeded. bt_socket_connection_state_changed_cb will be called.");
            dlog_print(DLOG_DEBUG, LOG_TAG,
                       "[bt_socket_connect_rfcomm] Succeeded. bt_socket_connection_state_changed_cb will be called.");
        }
    }

}


bt_error_e ret;
const char *file_name = "tempfile";

/* Called when a file is being transfered. */
void bt_opp_server_transfer_progress_cb_for_opp(const char *file, long long size, int percent,
        void *user_data)
{
    dlog_print(DLOG_DEBUG, LOG_TAG, "size: %ld", (long)size);
    PRINT_MSG("size: %ld", (long)size);
    dlog_print(DLOG_DEBUG, LOG_TAG, "percent: %d", percent);
    PRINT_MSG("percent: %d", percent);
    dlog_print(DLOG_DEBUG, LOG_TAG, "file: %s", file);
    PRINT_MSG("file: %s", file);
}

/* Called when a file is finished. */
void bt_opp_server_transfer_finished_cb_for_opp(int result, const char *file, long long size,
        void *user_data)
{
    if (result < 0) {
        PRINT_MSG("Failed");
        return;
    }

    PRINT_MSG("Succeeded");
    dlog_print(DLOG_DEBUG, LOG_TAG, "size: %ld", (long)size);
    PRINT_MSG("size: %ld", (long)size);
    dlog_print(DLOG_DEBUG, LOG_TAG, "file: %s", file);
    PRINT_MSG("file: %s", file);
}

/* Called when an OPP connection is requested */
void connection_requested_cb_for_opp_server(const char *remote_address, void *user_data)
{
    dlog_print(DLOG_DEBUG, LOG_TAG, "remote_address: %s", remote_address);
    /* Accept a file push request */
    PRINT_MSG("Accepting request from %s", remote_address);
    ret = bt_opp_server_accept(bt_opp_server_transfer_progress_cb_for_opp,
                               bt_opp_server_transfer_finished_cb_for_opp, file_name, NULL, NULL);
    if (ret != BT_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "[bt_opp_server_accept] Failed.");
        PRINT_MSG("[bt_opp_server_accept] Failed.");
    }
}

/* Notified when the Bluetooth adapter has enabled or disabled */
void adapter_state_changed_cb(int result, bt_adapter_state_e adapter_state, void *user_data)
{
    if (result != BT_ERROR_NONE) {
        PRINT_MSG("[adapter_state_changed_cb] Failed! result=%d", result);
        dlog_print(DLOG_ERROR, LOG_TAG, "[adapter_state_changed_cb] Failed! result=%d", result);
        return;
    }

    if (adapter_state == BT_ADAPTER_ENABLED) {
        PRINT_MSG("[adapter_state_changed_cb] Bluetooth is enabled!");
        dlog_print(DLOG_DEBUG, LOG_TAG, "[adapter_state_changed_cb] Bluetooth is enabled!");

        /* Get information about Bluetooth adapter */
        char *local_address = NULL;
        bt_adapter_get_address(&local_address);
        PRINT_MSG("[adapter_state_changed_cb] Adapter address: %s.", local_address);
        dlog_print(DLOG_DEBUG, LOG_TAG, "[adapter_state_changed_cb] Adapter address: %s.",
                   local_address);
        free(local_address);
        char *local_name = NULL;
        bt_adapter_get_name(&local_name);
        PRINT_MSG("[adapter_state_changed_cb] Adapter name: %s.", local_name);
        dlog_print(DLOG_DEBUG, LOG_TAG, "[adapter_state_changed_cb] Adapter name: %s.", local_name);
        free(local_name);

        /* Visibility mode of the Bluetooth device */
        bt_adapter_visibility_mode_e mode;
        int duration = 1;       // Duration until the visibility mode is changed so that other devices cannot find your device
        bt_adapter_get_visibility(&mode, &duration);

        switch (mode) {
        case BT_ADAPTER_VISIBILITY_MODE_NON_DISCOVERABLE:
            PRINT_MSG("[adapter_state_changed_cb] Visibility: NON_DISCOVERABLE");
            dlog_print(DLOG_DEBUG, LOG_TAG,
                       "[adapter_state_changed_cb] Visibility: NON_DISCOVERABLE");
            break;

        case BT_ADAPTER_VISIBILITY_MODE_GENERAL_DISCOVERABLE:
            PRINT_MSG("[adapter_state_changed_cb] Visibility: GENERAL_DISCOVERABLE");
            dlog_print(DLOG_DEBUG, LOG_TAG,
                       "[adapter_state_changed_cb] Visibility: GENERAL_DISCOVERABLE");
            break;

        case BT_ADAPTER_VISIBILITY_MODE_LIMITED_DISCOVERABLE:
            PRINT_MSG("[adapter_state_changed_cb] Visibility: LIMITED_DISCOVERABLE");
            dlog_print(DLOG_DEBUG, LOG_TAG,
                       "[adapter_state_changed_cb] Visibility: LIMITED_DISCOVERABLE");
            break;
        }

        char *directory = NULL;
        storage_get_directory(0, STORAGE_DIRECTORY_DOWNLOADS, &directory);

        /* Initialize OPP server */
        int ret = bt_opp_server_initialize_by_connection_request(directory,
                  connection_requested_cb_for_opp_server,
                  NULL);
        free(directory);
        if (ret != BT_ERROR_NONE) {
            dlog_print(DLOG_ERROR, LOG_TAG,
                       "[bt_opp_server_initialize_by_connection_request] Failed.");
            PRINT_MSG("[bt_opp_server_initialize_by_connection_request] Failed.");
        }

        _bluetooth_services_init();

    } else {
        PRINT_MSG("[adapter_state_changed_cb] Bluetooth is disabled!");
        dlog_print(DLOG_DEBUG, LOG_TAG, "[adapter_state_changed_cb] Bluetooth is disabled!");
        // When you try to get device information
        // by invoking bt_adapter_get_name(), bt_adapter_get_address(), or bt_adapter_get_visibility(),
        // BT_ERROR_NOT_ENABLED will occur
    }
}

/* Object Push Profile (OPP) */

/* Called when each file is being transfered */
void __bt_opp_client_push_progress_cb(const char *file,
                                      long long size, int percent, void *user_data)
{
    dlog_print(DLOG_DEBUG, LOG_TAG, "size: %ld", (long)size);
    PRINT_MSG("size: %ld", (long)size);
    dlog_print(DLOG_DEBUG, LOG_TAG, "percent: %d", percent);
    PRINT_MSG("percent: %d", percent);
    dlog_print(DLOG_DEBUG, LOG_TAG, "file: %s", file);
    PRINT_MSG("file: %s", file);
}

/* Called when the push request is finished */
void __bt_opp_client_push_finished_cb(int result, const char *remote_address, void *user_data)
{
    PRINT_MSG("Finished");
    dlog_print(DLOG_DEBUG, LOG_TAG, "result: %d", result);
    dlog_print(DLOG_DEBUG, LOG_TAG, "remote_address: %s", remote_address);

    /* Delete file info */
    int ret = bt_opp_client_clear_files();
    if (ret != BT_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "bt_opp_client_clear_files() failed");
        PRINT_MSG("bt_opp_client_clear_files() failed");
        return;
    }
}

/* Called when OPP server responds to the push request */
void __bt_opp_client_push_responded_cb(int result, const char *remote_address, void *user_data)
{
    if (result < 0) {
        dlog_print(DLOG_DEBUG, LOG_TAG, "Failed with result: %d", result);
        PRINT_MSG("Failed with result: %d", result);
    } else {
        dlog_print(DLOG_DEBUG, LOG_TAG, "Succeeded");
        PRINT_MSG("Succeeded");
    }

    dlog_print(DLOG_DEBUG, LOG_TAG, "remote_address: %s", remote_address);
    PRINT_MSG("remote_address: %s", remote_address);
}

void create_buttons_in_main_window6(appdata_s *ad, Evas_Object *obj, void *event_info){

	Evas_Object *display = _create_new_cd_display(ad, "Object Push Profile", _pop_cb,ad->navi5);
    remote_server_address = strdup(elm_object_part_text_get(ap_name_entry, NULL));

    if (!strcmp(remote_server_address, "")) {
        PRINT_MSG("Set other device MAC address");
        return;
    }

    bt_opp_client_deinitialize();
    /* Initialize the client */
    bt_error_e ret;
    ret = bt_opp_client_initialize();
    if (ret != BT_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "bt_opp_client_initialize() failed");
        PRINT_MSG("bt_opp_client_initialize() failed");
    }

    /* Get the information of a file that can be sent to the server device */
    char file_path[100] = { '\0', };
    char *resource_path = app_get_shared_resource_path();
    snprintf(file_path, sizeof(file_path) - 1, "%s/bluetooth.png", resource_path);
    free(resource_path);

    ret = bt_opp_client_add_file(file_path);
    if (ret != BT_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "bt_opp_client_add_file() failed");
        PRINT_MSG("bt_opp_client_add_file() failed");
        return;
    }

    /* Send the files to the server */
    ret = bt_opp_client_push_files(remote_server_address, __bt_opp_client_push_responded_cb,
                                   __bt_opp_client_push_progress_cb,
                                   __bt_opp_client_push_finished_cb, NULL);
    if (ret != BT_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "[bt_opp_client_push_files] Failed.");
        PRINT_MSG("[bt_opp_client_push_files] Failed.");
    } else {
        dlog_print(DLOG_DEBUG, LOG_TAG, "[bt_opp_client_push_files] Succeeded.");
        PRINT_MSG("[bt_opp_client_push_files] Succeeded.");
    }


}

void __write_completed_cb(int result, bt_gatt_h request_handle, void *user_data)
{
    dlog_print(DLOG_DEBUG, LOG_TAG, "[__write_completed_cb] has been invoked");
    PRINT_MSG("[__write_completed_cb] has been invoked");
}

Evas_Object *services_list;

/* Client Role */

void _bluetooth_gatt_cb(appdata_s *ad, Evas_Object *obj, void *event_info)
{
    // Setting the window
    ad->win = elm_win_util_standard_add(PACKAGE, PACKAGE);
    elm_win_conformant_set(ad->win, EINA_TRUE);
    elm_win_autodel_set(ad->win, EINA_TRUE);
    elm_win_indicator_mode_set(ad->win, ELM_WIN_INDICATOR_SHOW);
    elm_win_indicator_opacity_set(ad->win, ELM_WIN_INDICATOR_OPAQUE);



		Evas_Object *conform = elm_conformant_add(ad->win);

	    evas_object_size_hint_weight_set(conform, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
	    elm_win_resize_object_add(ad->win, conform);
	    evas_object_show(conform);

	// Create a naviframe
	    ad->navi6 = elm_naviframe_add(conform);
	    evas_object_size_hint_align_set(ad->navi6, EVAS_HINT_FILL, EVAS_HINT_FILL);
	    evas_object_size_hint_weight_set(ad->navi6, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);

	    elm_object_content_set(conform, ad->navi6);
	    evas_object_show(ad->navi6);

	    // Fill the list with items
	  create_buttons_in_main_window7(ad,obj,event_info);


	    eext_object_event_callback_add(ad->navi6, EEXT_CALLBACK_BACK, eext_naviframe_back_cb, NULL);

	    // Show the window after base gui is set up
	    evas_object_show(ad->win);

}


void create_buttons_in_main_window7(appdata_s *ad, Evas_Object *obj, void *event_info){

	Evas_Object *display = _create_new_cd_display(ad, "GATT Operation", _pop_cb,ad->navi6);
    dlog_print(DLOG_DEBUG, LOG_TAG, "[_bluetooth_gatt_cb]");

    /* Initialize the client */
    int ret = 0;

    const char *remote_server_address = elm_entry_entry_get(ap_name_entry);

    if (!strcmp(remote_server_address, "")) {
        PRINT_MSG
        ("Enter other device's MAC address. That device needs to have GATT server launched.");
        return;
    }

    /* UI part responsible for displaying list of services */
    Evas_Object *pop_win = elm_win_inwin_add(ad->win);
    evas_object_show(pop_win);

    Evas_Object *pop_box = elm_box_add(pop_win);
    evas_object_size_hint_align_set(pop_box, EVAS_HINT_FILL, EVAS_HINT_FILL);
    evas_object_size_hint_weight_set(pop_box, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);

    elm_win_inwin_content_set(pop_win, pop_box);
    evas_object_show(pop_box);

    Evas_Object *label = elm_label_add(pop_box);
    elm_object_text_set(label, "Available services");
    evas_object_show(label);

    services_list = elm_list_add(pop_box);
    evas_object_size_hint_align_set(services_list, EVAS_HINT_FILL, EVAS_HINT_FILL);
    evas_object_size_hint_weight_set(services_list, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
    evas_object_data_set(services_list, "inwin", pop_win);
    evas_object_show(services_list);
    elm_box_pack_end(pop_box, label);
    elm_box_pack_end(pop_box, services_list);

    if (client == NULL) {
        ret = bt_gatt_client_create(remote_server_address, &client);
        if (ret != BT_ERROR_NONE) {
            dlog_print(DLOG_ERROR, LOG_TAG, "[bt_gatt_client_create] Failed");
            PRINT_MSG("[bt_gatt_client_create] Failed.");
            return;
        }

        dlog_print(DLOG_DEBUG, LOG_TAG, "GATT client creation succeeded");
        PRINT_MSG("GATT client creation succeeded");
    }

    /* Get the address of the remote device */
    char *addr = NULL;

    ret = bt_gatt_client_get_remote_address(client, &addr);
    if (ret != BT_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "[bt_gatt_client_get_remote_address] Failed");
        PRINT_MSG("[bt_gatt_client_get_remote_address] Failed.");
    }

    free(addr);

    /* Discover available services and add their uuids to the list */
    ret = bt_gatt_client_foreach_services(client, __bt_gatt_client_foreach_svc_cb, &services_list);
    if (ret != BT_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "[bt_gatt_client_foreach_services] Failed");
        PRINT_MSG("[bt_gatt_client_foreach_services] Failed");
    }

    return;
}


void __bt_adapter_le_scan_result_cb(int result,
                                    bt_adapter_le_device_scan_result_info_s *info, void *user_data)
{
    bt_adapter_le_packet_type_e pkt_type = BT_ADAPTER_LE_PACKET_ADVERTISING;

    if (info == NULL) {
        dlog_print(DLOG_DEBUG, LOG_TAG, "No discovery_info!");
        PRINT_MSG("No discovery_info!");
        return;
    }

    if (info->adv_data_len > 31 || info->scan_data_len > 31) {
        dlog_print(DLOG_DEBUG, LOG_TAG, "###################");
        PRINT_MSG("###################");
        bt_adapter_le_stop_scan();
        dlog_print(DLOG_DEBUG, LOG_TAG, "###################");
        PRINT_MSG("###################");
        return;
    }

    int i;

    for (i = 0; i < 2; i++) {
        char **uuids;
        char *device_name;
        int tx_power_level;
        bt_adapter_le_service_data_s *data_list;
        int appearance;
        int manufacturer_id;
        char *manufacturer_data;
        int manufacturer_data_len;
        int count;

        pkt_type += i;

        if (pkt_type == BT_ADAPTER_LE_PACKET_ADVERTISING && info->adv_data == NULL)
            continue;

        if (pkt_type == BT_ADAPTER_LE_PACKET_SCAN_RESPONSE && info->scan_data == NULL)
            break;

        if (bt_adapter_le_get_scan_result_service_uuids(info, pkt_type, &uuids, &count) ==
                BT_ERROR_NONE) {
            int i;

            for (i = 0; i < count; i++) {
                dlog_print(DLOG_DEBUG, LOG_TAG, "UUID[%d] = %s", i + 1, uuids[i]);
                PRINT_MSG("UUID[%d] = %s", i + 1, uuids[i]);
                g_free(uuids[i]);
            }

            g_free(uuids);
        }

        if (bt_adapter_le_get_scan_result_device_name(info, pkt_type, &device_name) ==
                BT_ERROR_NONE) {
            dlog_print(DLOG_DEBUG, LOG_TAG, "Device name = %s", device_name);
            PRINT_MSG("Device name = %s", device_name);
            g_free(device_name);
        }

        if (bt_adapter_le_get_scan_result_tx_power_level(info, pkt_type, &tx_power_level) ==
                BT_ERROR_NONE) {
            dlog_print(DLOG_DEBUG, LOG_TAG, "TX Power level = %d", tx_power_level);
            PRINT_MSG("TX Power level = %d", tx_power_level);
        }

        if (bt_adapter_le_get_scan_result_service_solicitation_uuids(info, pkt_type, &uuids, &count)
                == BT_ERROR_NONE) {
            int i;

            for (i = 0; i < count; i++) {
                dlog_print(DLOG_DEBUG, LOG_TAG, "Solicitation UUID[%d] = %s", i + 1, uuids[i]);
                PRINT_MSG("Solicitation UUID[%d] = %s", i + 1, uuids[i]);
                g_free(uuids[i]);
            }

            g_free(uuids);
        }

        if (bt_adapter_le_get_scan_result_service_data_list(info, pkt_type, &data_list, &count) ==
                BT_ERROR_NONE) {
            int i;

            for (i = 0; i < count; i++) {
                dlog_print(DLOG_DEBUG, LOG_TAG, "Service Data[%d] = [0x%2.2X%2.2X:0x%.2X...]",
                           i + 1, data_list[i].service_uuid[0], data_list[i].service_uuid[1],
                           data_list[i].service_data[0]);
                PRINT_MSG("Service Data[%d] = [0x%2.2X%2.2X:0x%.2X...]", i + 1,
                          data_list[i].service_uuid[0], data_list[i].service_uuid[1],
                          data_list[i].service_data[0]);
            }

            bt_adapter_le_free_service_data_list(data_list, count);
        }

        if (bt_adapter_le_get_scan_result_appearance(info, pkt_type, &appearance) == BT_ERROR_NONE) {
            dlog_print(DLOG_DEBUG, LOG_TAG, "Appearance = %d", appearance);
            PRINT_MSG("Appearance = %d", appearance);
        }

        if (bt_adapter_le_get_scan_result_manufacturer_data(info, pkt_type, &manufacturer_id,
                &manufacturer_data,
                &manufacturer_data_len) ==
                BT_ERROR_NONE) {
            dlog_print(DLOG_DEBUG, LOG_TAG, "Manufacturer data[ID:%.4X, 0x%.2X%.2X...(len:%d)]",
                       manufacturer_id, manufacturer_data[0], manufacturer_data[1],
                       manufacturer_data_len);
            PRINT_MSG("Manufacturer data[ID:%.4X, 0x%.2X%.2X...(len:%d)]", manufacturer_id,
                      manufacturer_data[0], manufacturer_data[1], manufacturer_data_len);
            g_free(manufacturer_data);
        }
    }
}

void le_add_advertising_data()
{
    int adv_data_type = 3;      // Default all
    int manufacturer_id = 117;
    char *manufacture = NULL;
    char manufacture_0[] = { 0x0, 0x0, 0x0, 0x0 };
    char manufacture_1[] = { 0x01, 0x01, 0x01, 0x01 };
    char manufacture_2[] = { 0x02, 0x02, 0x02, 0x02 };
    char manufacture_3[] = { 0x03, 0x03, 0x03, 0x03 };
    char service_data[] = { 0x01, 0x02, 0x03 };
    const char *time_svc_uuid_16 = "1805";
    const char *battery_svc_uuid_16 = "180f";
    const char *heart_rate_svc_uuid_16 = "180d";
    const char *immediate_alert_svc_uuid_16 = "1802";
    const char *ancs_uuid_128 = "7905F431-B5CE-4E99-A40F-4B1E122D00D0";
    int appearance = 192;       // 192 is a generic watch

    advertiser = advertiser_list[advertiser_index];

    if (advertiser == NULL) {
        ret = bt_adapter_le_create_advertiser(&advertiser);
        if (ret != BT_ERROR_NONE) {
            dlog_print(DLOG_DEBUG, LOG_TAG, "[bt_adapter_le_create_advertiser] failed = %d", ret);
            PRINT_MSG("[bt_adapter_le_create_advertiser] failed = %d", ret);
        } else
            advertiser_list[advertiser_index] = advertiser;
    } else {
        ret = bt_adapter_le_clear_advertising_data(advertiser, BT_ADAPTER_LE_PACKET_ADVERTISING);
        if (ret != BT_ERROR_NONE) {
            dlog_print(DLOG_DEBUG, LOG_TAG, "clear advertising data [0x%04x]", ret);
            PRINT_MSG("clear advertising data [0x%04x]", ret);
        }

        ret = bt_adapter_le_clear_advertising_data(advertiser, BT_ADAPTER_LE_PACKET_SCAN_RESPONSE);
        if (ret != BT_ERROR_NONE) {
            dlog_print(DLOG_DEBUG, LOG_TAG, "clear scan response data [0x%04x]", ret);
            PRINT_MSG("clear scan response data [0x%04x]", ret);
        }
    }

    switch (adv_data_type) {
    case 0:                    // Service UUID
        ret =
            bt_adapter_le_add_advertising_service_uuid(advertiser, BT_ADAPTER_LE_PACKET_ADVERTISING,
                    time_svc_uuid_16);
        if (ret != BT_ERROR_NONE) {
            dlog_print(DLOG_DEBUG, LOG_TAG, "add service_uuid [0x%04x]", ret);
            PRINT_MSG("add service_uuid [0x%04x]", ret);
        }

        ret =
            bt_adapter_le_add_advertising_service_uuid(advertiser, BT_ADAPTER_LE_PACKET_ADVERTISING,
                    battery_svc_uuid_16);
        if (ret != BT_ERROR_NONE) {
            dlog_print(DLOG_DEBUG, LOG_TAG, "add service_uuid [0x%04x]", ret);
            PRINT_MSG("add service_uuid [0x%04x]", ret);
        }

        manufacture = manufacture_0;
        break;

    case 1:                    // Service solicitation
        ret =
            bt_adapter_le_add_advertising_service_solicitation_uuid(advertiser,
                    BT_ADAPTER_LE_PACKET_ADVERTISING,
                    heart_rate_svc_uuid_16);
        if (ret != BT_ERROR_NONE) {
            dlog_print(DLOG_DEBUG, LOG_TAG, "add service_solicitation_uuid [0x%04x]", ret);
            PRINT_MSG("add service_solicitation_uuid [0x%04x]", ret);
        }

        ret =
            bt_adapter_le_add_advertising_service_solicitation_uuid(advertiser,
                    BT_ADAPTER_LE_PACKET_ADVERTISING,
                    immediate_alert_svc_uuid_16);
        if (ret != BT_ERROR_NONE) {
            dlog_print(DLOG_DEBUG, LOG_TAG, "add service_solicitation_uuid [0x%04x]", ret);
            PRINT_MSG("add service_solicitation_uuid [0x%04x]", ret);
        }

        manufacture = manufacture_1;
        break;

    case 2:                    // Appearance & TX power level
        ret = bt_adapter_le_set_advertising_appearance(advertiser, BT_ADAPTER_LE_PACKET_ADVERTISING,
                appearance);
        if (ret != BT_ERROR_NONE) {
            dlog_print(DLOG_DEBUG, LOG_TAG, "add appearance data [0x%04x]", ret);
            PRINT_MSG("add appearance data [0x%04x]", ret);
        }

        ret =
            bt_adapter_le_set_advertising_tx_power_level(advertiser,
                    BT_ADAPTER_LE_PACKET_ADVERTISING, true);
        if (ret != BT_ERROR_NONE) {
            dlog_print(DLOG_DEBUG, LOG_TAG, "add appearance data [0x%04x]", ret);
            PRINT_MSG("add appearance data [0x%04x]", ret);
        }

        manufacture = manufacture_2;
        break;

    case 3:                    // All
        ret =
            bt_adapter_le_add_advertising_service_uuid(advertiser, BT_ADAPTER_LE_PACKET_ADVERTISING,
                    time_svc_uuid_16);
        if (ret != BT_ERROR_NONE) {
            dlog_print(DLOG_DEBUG, LOG_TAG, "add service_uuid [0x%04x]", ret);
            PRINT_MSG("add service_uuid [0x%04x]", ret);
        }

        ret =
            bt_adapter_le_add_advertising_service_uuid(advertiser, BT_ADAPTER_LE_PACKET_ADVERTISING,
                    battery_svc_uuid_16);
        if (ret != BT_ERROR_NONE) {
            dlog_print(DLOG_DEBUG, LOG_TAG, "add service_uuid [0x%04x]", ret);
            PRINT_MSG("add service_uuid [0x%04x]", ret);
        }

        ret =
            bt_adapter_le_add_advertising_service_solicitation_uuid(advertiser,
                    BT_ADAPTER_LE_PACKET_ADVERTISING,
                    heart_rate_svc_uuid_16);
        if (ret != BT_ERROR_NONE) {
            dlog_print(DLOG_DEBUG, LOG_TAG, "add service_solicitation_uuid [0x%04x]", ret);
            PRINT_MSG("add service_solicitation_uuid [0x%04x]", ret);
        }

        ret =
            bt_adapter_le_add_advertising_service_solicitation_uuid(advertiser,
                    BT_ADAPTER_LE_PACKET_ADVERTISING,
                    immediate_alert_svc_uuid_16);
        if (ret != BT_ERROR_NONE) {
            dlog_print(DLOG_DEBUG, LOG_TAG, "add service_solicitation_uuid [0x%04x]", ret);
            PRINT_MSG("add service_solicitation_uuid [0x%04x]", ret);
        }

        ret = bt_adapter_le_set_advertising_appearance(advertiser, BT_ADAPTER_LE_PACKET_ADVERTISING,
                appearance);
        if (ret != BT_ERROR_NONE) {
            dlog_print(DLOG_DEBUG, LOG_TAG, "add appearance data [0x%04x]", ret);
            PRINT_MSG("add appearance data [0x%04x]", ret);
        }

        ret =
            bt_adapter_le_set_advertising_tx_power_level(advertiser,
                    BT_ADAPTER_LE_PACKET_ADVERTISING, true);
        if (ret != BT_ERROR_NONE) {
            dlog_print(DLOG_DEBUG, LOG_TAG, "add tx_power_level [0x%04x]", ret);
            PRINT_MSG("add tx_power_level [0x%04x]", ret);
        }

        manufacture = manufacture_3;
        break;

    case 4:                    // ANCS
        ret =
            bt_adapter_le_add_advertising_service_solicitation_uuid(advertiser,
                    BT_ADAPTER_LE_PACKET_ADVERTISING,
                    time_svc_uuid_16);
        if (ret != BT_ERROR_NONE) {
            dlog_print(DLOG_DEBUG, LOG_TAG, "add service_solicitation_uuid [0x%04x]", ret);
            PRINT_MSG("add service_solicitation_uuid [0x%04x]", ret);
        }

        ret =
            bt_adapter_le_add_advertising_service_solicitation_uuid(advertiser,
                    BT_ADAPTER_LE_PACKET_ADVERTISING,
                    ancs_uuid_128);
        if (ret != BT_ERROR_NONE) {
            dlog_print(DLOG_DEBUG, LOG_TAG, "add service_solicitation_uuid [0x%04x]", ret);
            PRINT_MSG("add service_solicitation_uuid [0x%04x]", ret);
        }

        ret =
            bt_adapter_le_set_advertising_device_name(advertiser,
                    BT_ADAPTER_LE_PACKET_SCAN_RESPONSE, true);
        if (ret != BT_ERROR_NONE) {
            dlog_print(DLOG_DEBUG, LOG_TAG, "set device name [0x%04x]", ret);
            PRINT_MSG("set device name [0x%04x]", ret);
        }

        break;

    default:
        dlog_print(DLOG_DEBUG, LOG_TAG, "No adv data");
        PRINT_MSG("No adv data");
        break;
    }

    // Default scan response data
    ret = bt_adapter_le_add_advertising_service_data(advertiser, BT_ADAPTER_LE_PACKET_SCAN_RESPONSE,
            time_svc_uuid_16, service_data,
            sizeof(service_data));
    if (ret != BT_ERROR_NONE) {
        dlog_print(DLOG_DEBUG, LOG_TAG, "add service_data [0x%04x]", ret);
        PRINT_MSG("add service_data [0x%04x]", ret);
    }

    ret =
        bt_adapter_le_set_advertising_device_name(advertiser, BT_ADAPTER_LE_PACKET_SCAN_RESPONSE,
                true);
    if (ret != BT_ERROR_NONE) {
        dlog_print(DLOG_DEBUG, LOG_TAG, "set device name [0x%04x]", ret);
        PRINT_MSG("set device name [0x%04x]", ret);
    }

    ret =
        bt_adapter_le_add_advertising_manufacturer_data(advertiser,
                BT_ADAPTER_LE_PACKET_SCAN_RESPONSE,
                manufacturer_id, manufacture,
                sizeof(manufacture_0));
    if (ret != BT_ERROR_NONE) {
        dlog_print(DLOG_DEBUG, LOG_TAG, "add manufacturer data [0x%04x]", ret);
        PRINT_MSG("add manufacturer data [0x%04x]", ret);
    }
}

static void __bt_adapter_le_advertising_state_changed_cb(int result,
        bt_advertiser_h advertiser,
        bt_adapter_le_advertising_state_e
        adv_state, void *user_data)
{
    dlog_print(DLOG_INFO, LOG_TAG, "Result : %d", result);
    PRINT_MSG("Result : %d", result);
    dlog_print(DLOG_INFO, LOG_TAG, "Advertiser : %p", advertiser);
    PRINT_MSG("Advertiser : %p", advertiser);
    dlog_print(DLOG_INFO, LOG_TAG, "Advertising %s [%d]",
               adv_state == BT_ADAPTER_LE_ADVERTISING_STARTED ? "started" : "stopped", adv_state);
    PRINT_MSG("Advertising %s [%d]",
              adv_state == BT_ADAPTER_LE_ADVERTISING_STARTED ? "started" : "stopped", adv_state);
}

static void __bt_adapter_le_advertising_state_changed_cb_2(int result,
        bt_advertiser_h advertiser,
        bt_adapter_le_advertising_state_e
        adv_state, void *user_data)
{
    dlog_print(DLOG_INFO, LOG_TAG, "Result : %d", result);
    PRINT_MSG("Result : %d", result);
    dlog_print(DLOG_INFO, LOG_TAG, "Advertiser : %p", advertiser);
    PRINT_MSG("Advertiser : %p", advertiser);
    dlog_print(DLOG_INFO, LOG_TAG, "Advertising %s [%d]",
               adv_state == BT_ADAPTER_LE_ADVERTISING_STARTED ? "started" : "stopped", adv_state);
    PRINT_MSG("Advertising %s [%d]",
              adv_state == BT_ADAPTER_LE_ADVERTISING_STARTED ? "started" : "stopped", adv_state);
}

static void __bt_adapter_le_advertising_state_changed_cb_3(int result,
        bt_advertiser_h advertiser,
        bt_adapter_le_advertising_state_e
        adv_state, void *user_data)
{
    dlog_print(DLOG_INFO, LOG_TAG, "Result : %d", result);
    PRINT_MSG("Result : %d", result);
    dlog_print(DLOG_INFO, LOG_TAG, "Advertiser : %p", advertiser);
    PRINT_MSG("Advertiser : %p", advertiser);
    dlog_print(DLOG_INFO, LOG_TAG, "Advertising %s [%d]",
               adv_state == BT_ADAPTER_LE_ADVERTISING_STARTED ? "started" : "stopped", adv_state);
    PRINT_MSG("Advertising %s [%d]",
              adv_state == BT_ADAPTER_LE_ADVERTISING_STARTED ? "started" : "stopped", adv_state);
}

Eina_Bool _bluetooth_le_stop_scan(void *data)
{
    ret = bt_adapter_le_stop_scan();
    if (ret != BT_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "[bt_adapter_le_stop_scan] Failed with error %d.", ret);
        PRINT_MSG("[bt_adapter_le_stop_scan] Failed.");
    }

    return EINA_FALSE;
}


void create_buttons_in_main_window8(appdata_s *ad, Evas_Object *obj, void *event_info){

	Evas_Object *display = _create_new_cd_display(ad, "Le Scans", _pop_cb,ad->navi7);

    // BLE scan
    int ret = BT_ERROR_NONE;
    ret = bt_adapter_le_start_scan(__bt_adapter_le_scan_result_cb, NULL);
    if (ret != BT_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "[bt_adapter_le_start_scan] Failed.");
        PRINT_MSG("[bt_adapter_le_start_scan] Failed.");
        return;
    }

    /* Adding Advertising Data to the LE Advertisement */
    le_add_advertising_data();

    /* Setting the Advertising Connectable Mode */
    static bt_advertiser_h advertiser = NULL;
    static bt_advertiser_h advertiser_list[3] = { NULL, };
    static int advertiser_index = 0;
    int type = BT_ADAPTER_LE_ADVERTISING_CONNECTABLE;

    advertiser = advertiser_list[advertiser_index];

    if (advertiser == NULL) {
        ret = bt_adapter_le_create_advertiser(&advertiser);
        if (ret != BT_ERROR_NONE) {
            dlog_print(DLOG_DEBUG, LOG_TAG, "[bt_adapter_le_create_advertiser] failed = %d", ret);
            PRINT_MSG("[bt_adapter_le_create_advertiser] failed = %d", ret);
        } else {
            advertiser_list[advertiser_index] = advertiser;
            ret = bt_adapter_le_set_advertising_connectable(advertiser, type);
            if (ret != BT_ERROR_NONE) {
                dlog_print(DLOG_DEBUG, LOG_TAG, "add scan response data [0x%04x]", ret);
                PRINT_MSG("add scan response data [0x%04x]", ret);
            } else {
                /* Setting the LE Advertising Mode */
                int mode = BT_ADAPTER_LE_ADVERTISING_MODE_BALANCED;
                ret = bt_adapter_le_set_advertising_mode(advertiser, mode);
                if (ret != BT_ERROR_NONE) {
                    dlog_print(DLOG_DEBUG, LOG_TAG, "add scan response data [0x%04x]", ret);
                    PRINT_MSG("add scan response data [0x%04x]", ret);
                }

                // Starting and Stopping Advertising
                bt_adapter_le_advertising_state_changed_cb cb;

                if (advertiser_index == 0)
                    cb = __bt_adapter_le_advertising_state_changed_cb;
                else if (advertiser_index == 1)
                    cb = __bt_adapter_le_advertising_state_changed_cb_2;
                else
                    cb = __bt_adapter_le_advertising_state_changed_cb_3;

                advertiser = advertiser_list[advertiser_index];
                advertiser_index++;
                advertiser_index %= 3;
                ret = bt_adapter_le_start_advertising_new(advertiser, cb, NULL);
                if (ret < BT_ERROR_NONE)
                    dlog_print(DLOG_DEBUG, LOG_TAG, "failed with [0x%04x]", ret);

                if (advertiser != NULL) {
                    ret = bt_adapter_le_stop_advertising(advertiser);
                    if (ret < BT_ERROR_NONE) {
                        dlog_print(DLOG_DEBUG, LOG_TAG, "failed with [0x%04x]", ret);
                        PRINT_MSG("failed with [0x%04x]", ret);
                    }
                }
            }
        }
    }

    PRINT_MSG("The scan will last 10 seconds");
    ecore_timer_add(10.0, _bluetooth_le_stop_scan, NULL);

}

void create_buttons_in_main_window9(appdata_s *ad){

	Evas_Object *display = _create_new_cd_display(ad, "Error Codes", _pop_cb,ad->navi9);

    PRINT_MSG("BT_ERROR_NONE: %d", BT_ERROR_NONE);
    PRINT_MSG("BT_ERROR_CANCELLED: %d", BT_ERROR_CANCELLED);
    PRINT_MSG("BT_ERROR_INVALID_PARAMETER: %d", BT_ERROR_INVALID_PARAMETER);
    PRINT_MSG("BT_ERROR_OUT_OF_MEMORY: %d", BT_ERROR_OUT_OF_MEMORY);
    PRINT_MSG("BT_ERROR_RESOURCE_BUSY: %d", BT_ERROR_RESOURCE_BUSY);
    PRINT_MSG("BT_ERROR_TIMED_OUT: %d", BT_ERROR_TIMED_OUT);
    PRINT_MSG("BT_ERROR_NOW_IN_PROGRESS: %d", BT_ERROR_NOW_IN_PROGRESS);
    PRINT_MSG("BT_ERROR_NOT_SUPPORTED: %d", BT_ERROR_NOT_SUPPORTED);
    PRINT_MSG("BT_ERROR_PERMISSION_DENIED: %d", BT_ERROR_PERMISSION_DENIED);
    PRINT_MSG("BT_ERROR_QUOTA_EXCEEDED: %d", BT_ERROR_QUOTA_EXCEEDED);
    PRINT_MSG("BT_ERROR_NO_DATA: %d", BT_ERROR_NO_DATA);
    PRINT_MSG("BT_ERROR_NOT_INITIALIZED: %d", BT_ERROR_NOT_INITIALIZED);
    PRINT_MSG("BT_ERROR_NOT_ENABLED: %d", BT_ERROR_NOT_ENABLED);
    PRINT_MSG("BT_ERROR_ALREADY_DONE: %d", BT_ERROR_ALREADY_DONE);
    PRINT_MSG("BT_ERROR_OPERATION_FAILED: %d", BT_ERROR_OPERATION_FAILED);
    PRINT_MSG("BT_ERROR_NOT_IN_PROGRESS: %d", BT_ERROR_NOT_IN_PROGRESS);
    PRINT_MSG("BT_ERROR_REMOTE_DEVICE_NOT_BONDED: %d", BT_ERROR_REMOTE_DEVICE_NOT_BONDED);
    PRINT_MSG("BT_ERROR_AUTH_REJECTED: %d", BT_ERROR_AUTH_REJECTED);
    PRINT_MSG("BT_ERROR_AUTH_FAILED: %d", BT_ERROR_AUTH_FAILED);
    PRINT_MSG("BT_ERROR_REMOTE_DEVICE_NOT_FOUND: %d", BT_ERROR_REMOTE_DEVICE_NOT_FOUND);
    PRINT_MSG("BT_ERROR_SERVICE_SEARCH_FAILED: %d", BT_ERROR_SERVICE_SEARCH_FAILED);
    PRINT_MSG("BT_ERROR_REMOTE_DEVICE_NOT_CONNECTED: %d", BT_ERROR_REMOTE_DEVICE_NOT_CONNECTED);
    PRINT_MSG("BT_ERROR_AGAIN: %d", BT_ERROR_AGAIN);
    PRINT_MSG("BT_ERROR_SERVICE_NOT_FOUND: %d", BT_ERROR_SERVICE_NOT_FOUND);

}


bt_error_e ret;

void create_buttons_in_main_window10(appdata_s *ad, char *data, Evas_Object *obj, void *event_info){
	Evas_Object *display = _create_new_cd_display(ad, "Sending", _pop_cb,ad->navi8);
	int client_socket_fd = 0;

	ret = bt_socket_send_data(client_socket_fd, data, sizeof(data));
	if (ret != BT_ERROR_NONE) {
	    dlog_print(DLOG_ERROR, LOG_TAG, "[bt_socket_send_data] failed.");
	}
}


void _display_destroy_cb(void *data, Evas *e, Evas_Object *obj, void *event_info)
{
    int i;

    for (i = 0; i < g_list_length(devices_list); i++) {
        void *device_info = g_list_nth_data(devices_list, i);
        free(device_info);
    }
}

/* Called when entry text is changed. It allows to change peer device */
void _entry_changed(void *data, Evas_Object *obj, void *event_info)
{
    if (server_socket_fd != -1)
        bt_socket_disconnect_rfcomm(server_socket_fd);

    server_socket_fd = -1;
}

void create_buttons_in_main_window(appdata_s *ad)
{
	 Evas_Object *display = _create_new_cd_display(ad, "Main", _pop_cb, ad->navi);
	 	_new_button(ad, display, "Send Contact Via Bluetooth", _bluetooth_page);
}

static void win_delete_request_cb(void *data, Evas_Object *obj, void *event_info)
{
    elm_exit();
}

void create_buttons_in_main_window1(appdata_s *ad)
{

	Evas_Object *display = _create_new_cd_display(ad, "Bluetooth", _pop_cb,ad->navi1);

    evas_object_event_callback_add(display, EVAS_CALLBACK_FREE, _display_destroy_cb, NULL);

    bt_error_e ret;

    /* Initializing bluetooth */
    ret = bt_initialize();
    if (ret != BT_ERROR_NONE) {
        PRINT_MSG("[bt_initialize] Failed.");
        dlog_print(DLOG_ERROR, LOG_TAG, "[bt_initialize] Failed.");
        return;
    }

    /* Track state changes */
    ret = bt_adapter_set_state_changed_cb(adapter_state_changed_cb, NULL);
    if (ret != BT_ERROR_NONE) {
        PRINT_MSG("[bt_adapter_set_state_changed_cb()] Failed.");
        dlog_print(DLOG_ERROR, LOG_TAG, "[bt_adapter_set_state_changed_cb()] Failed.");
    }

    /* Check whether the Bluetooth Service is enabled */
    bt_adapter_state_e state;
    bt_adapter_get_state(&state);

    /* Register the callback for classic Bluetooth */
    ret =
        bt_adapter_set_device_discovery_state_changed_cb(adapter_device_discovery_state_changed_cb,
                (void *)&devices_list);
    if (ret != BT_ERROR_NONE) {
        PRINT_MSG("[bt_adapter_set_device_discovery_state_changed_cb] Failed.");
        dlog_print(DLOG_ERROR, LOG_TAG,
                   "[bt_adapter_set_device_discovery_state_changed_cb] Failed.");
    }

    if (state == BT_ADAPTER_ENABLED) {
        /* Initialize OPP server */
        char *directory = NULL;
        storage_get_directory(ad->internal_storage_id, STORAGE_DIRECTORY_DOWNLOADS, &directory);

        int ret = bt_opp_server_initialize_by_connection_request(directory,
                  connection_requested_cb_for_opp_server,
                  NULL);
        free(directory);
        if (ret != BT_ERROR_NONE) {
            dlog_print(DLOG_ERROR, LOG_TAG,
                       "[bt_opp_server_initialize_by_connection_request] Failed.");
            PRINT_MSG("[bt_opp_server_initialize_by_connection_request] Failed.");
        }

        _bluetooth_services_init();
    } else {                    /* If the Bluetooth Service is not enabled */
        ret = bt_onoff_operation();
        if (ret != 0) {
            PRINT_MSG("Bluetooth is not supported on your device.");
            return;
        }
    }

    _new_button(ad, display, "Change visibility", bt_set_visibility_operation);
    _new_button(ad, display, "Finding other devices", _bluetooth_finding_devices_cb);

    ap_name_entry = elm_entry_add(display);
    elm_object_part_text_set(ap_name_entry, "guide",
                             " MAC adress of second device");
    evas_object_smart_callback_add(ap_name_entry, "changed", _entry_changed, NULL);
    elm_box_pack_end(display, ap_name_entry);
    evas_object_size_hint_weight_set(ap_name_entry, EVAS_HINT_EXPAND, 0);
    evas_object_size_hint_align_set(ap_name_entry, EVAS_HINT_FILL, EVAS_HINT_FILL);
    evas_object_show(ap_name_entry);

    _new_button(ad, display, " ", _demo_cb);
 //   _new_button(ad, display, "Bond", _bluetooth_bond_device);


    create_buttons_in_main_window2(display, ad);

    eext_object_event_callback_add(display, EEXT_CALLBACK_BACK, win_delete_request_cb, NULL);


}


void

create_buttons_in_main_window2(Evas_Object *display, appdata_s *ad){


		contacts_init();

		msg_entry1 = elm_entry_add(display);

		elm_object_part_text_set(msg_entry1, "guide", "Enter Contact for Sending");

		elm_box_pack_end(display, msg_entry1);

		evas_object_size_hint_weight_set(msg_entry1, EVAS_HINT_EXPAND, 0);

		evas_object_size_hint_align_set(msg_entry1, EVAS_HINT_FILL, EVAS_HINT_FILL);

		evas_object_show(msg_entry1);


		_new_button(ad, display, "Send/Receive contacts", _send);
		_new_button(ad, display, "Saving contacts", _save);


	    eext_object_event_callback_add(display, EEXT_CALLBACK_BACK, win_delete_request_cb, NULL);


}
