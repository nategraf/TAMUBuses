#include <pebble.h>

#define ROUTES_LEN 24
#define SECTIONS_LEN 4
#define ROUTE_SHORT_NAME_LEN 8
#define ROUTE_NAME_LEN 48

enum {
  ON_CAMPUS_GROUP = 0,
  OFF_CAMPUS_GROUP,
  GAME_DAY_GROUP,
  OTHER_GROUP
};

typedef struct{
  char *title;
  char *subtitle;
} MenuItem;

static Window *s_window;
static MenuLayer *s_route_menu;

static GRect s_window_frame;

static int s_routes_len[SECTIONS_LEN] = {0, 0, 0, 0};
static char *s_section_titles[SECTIONS_LEN] = {"On Campus", "Off Campus", "Game Day", "Other"};
static MenuItem s_route_items[SECTIONS_LEN][ROUTES_LEN];

// Write message to buffer & send
static void request_routes(){
	DictionaryIterator *iter;
	
	app_message_outbox_begin(&iter);
	dict_write_cstring(iter, MESSAGE_KEY_request, "ROUTES");
	
	dict_write_end(iter);
  app_message_outbox_send();
}

static void request_route_info(const char *short_name){
	DictionaryIterator *iter;
	
	app_message_outbox_begin(&iter);
	dict_write_cstring(iter, MESSAGE_KEY_request, "ROUTE_INFO");
  dict_write_cstring(iter, MESSAGE_KEY_route_short_name, short_name);
	
	dict_write_end(iter);
  app_message_outbox_send();
}

void down_single_click_handler(ClickRecognizerRef recognizer, void *context) {
}

void config_provider(Window *window) {
  window_single_click_subscribe(BUTTON_ID_SELECT, down_single_click_handler);
}

//========================================= MESSAGE HANDLING ======================================================
// Called when a message is received from PebbleKitJS
static void in_received_handler(DictionaryIterator *received, void *context) {
	Tuple *tuple;
	
  // Is it the ready notification?
	tuple = dict_find(received, MESSAGE_KEY_jsReady);
	if(tuple) {
		APP_LOG(APP_LOG_LEVEL_DEBUG, "Received ready notification: %d", (int)tuple->value->uint32); 
    request_routes();
	}
	
  // Is it a route for addition to the menu?
	tuple = dict_find(received, MESSAGE_KEY_request);
	if(tuple) {
    if(strcmp(tuple->value->cstring, "ROUTES") == 0){
      // Save the route name on the heap for use in the menu
      char *name = malloc(ROUTE_NAME_LEN); 
      name[0] = '\0';
      tuple = dict_find(received, MESSAGE_KEY_route_name);
      if(tuple){
        strcpy(name, tuple->value->cstring);
      }
      
      char *short_name = malloc(ROUTE_SHORT_NAME_LEN); 
      name[0] = '\0';
      tuple = dict_find(received, MESSAGE_KEY_route_short_name);
      if(tuple){
        strcpy(short_name, tuple->value->cstring);
      }
      
      char *group = "\0";
      tuple = dict_find(received, MESSAGE_KEY_route_group);
      if(tuple){
        group = tuple->value->cstring;
      }
  		APP_LOG(APP_LOG_LEVEL_DEBUG, "Received route: %s : %s", name, group);
      
      int i = OTHER_GROUP;
      if(strcmp(group, "Off Campus") == 0){
        i = OFF_CAMPUS_GROUP;
      }
      else if(strcmp(group, "Game Day Routes") == 0){
        i = GAME_DAY_GROUP;
      }
      else if(strcmp(group, "On Campus") == 0){
        i = ON_CAMPUS_GROUP;
      }
      
      s_route_items[i][s_routes_len[i]].title = short_name;
      s_route_items[i][s_routes_len[i]].subtitle = name;
      s_routes_len[i]++;
      
      layer_mark_dirty(menu_layer_get_layer(s_route_menu));
    }
	}
}

// Called when an incoming message from PebbleKitJS is dropped
static void in_dropped_handler(AppMessageResult reason, void *context) {	
}

// Called when PebbleKitJS does not acknowledge receipt of a message
static void out_failed_handler(DictionaryIterator *failed, AppMessageResult reason, void *context) {
}

//========================================= MENU CALLBACKS ======================================================
static uint16_t menu_get_num_sections_callback(MenuLayer *menu_layer, void *context){
  return SECTIONS_LEN;
}

static uint16_t menu_get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *context) {
  return s_routes_len[section_index];
}

static int16_t menu_get_header_height_callback(MenuLayer *menu_layer, uint16_t section_index, void *context) {
  return MENU_CELL_BASIC_HEADER_HEIGHT;
}

static void menu_draw_header_callback(GContext* gctx, const Layer *cell_layer, uint16_t section_index, void *context) {
  // Determine which section we're working with
  menu_cell_basic_header_draw(gctx, cell_layer, s_section_titles[section_index]);
}

static void menu_draw_row_callback(GContext* gctx, const Layer *cell_layer, MenuIndex *cell_index, void *context) {
  uint16_t i = cell_index->section;
  uint16_t j = cell_index->row;
  menu_cell_basic_draw(gctx, cell_layer, s_route_items[i][j].title, s_route_items[i][j].subtitle, NULL);
}

static void menu_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *context) {
  uint16_t i = cell_index->section;
  uint16_t j = cell_index->row;
  MenuItem *item = &s_route_items[i][j];
  if(item){
    item->subtitle = "SELECTED";
    layer_mark_dirty(menu_layer_get_layer(s_route_menu));
  }
}

#ifdef PBL_ROUND 
static int16_t get_cell_height_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *callback_context) {
  if (menu_layer_is_index_selected(menu_layer, cell_index)) {
    return MENU_CELL_ROUND_FOCUSED_SHORT_CELL_HEIGHT;
  } 
  else {
    return MENU_CELL_ROUND_UNFOCUSED_SHORT_CELL_HEIGHT;
  }
}
#endif  

//========================================= MAIN ======================================================
static void main_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  s_window_frame = layer_get_frame(window_layer);
  
  //TODO: Get the routes by API call or cache access
   
  s_route_menu = menu_layer_create(s_window_frame);
  menu_layer_set_callbacks(s_route_menu, NULL, (MenuLayerCallbacks){
    .get_num_sections = menu_get_num_sections_callback,
    .get_num_rows = menu_get_num_rows_callback,
    .get_header_height = PBL_IF_RECT_ELSE(menu_get_header_height_callback, NULL),
    .draw_header = PBL_IF_RECT_ELSE(menu_draw_header_callback, NULL),
    .draw_row = menu_draw_row_callback,
    .select_click = menu_select_callback,
    .get_cell_height = PBL_IF_ROUND_ELSE(get_cell_height_callback, NULL),
  });
  
  layer_add_child(window_layer, menu_layer_get_layer(s_route_menu));
}

static void main_window_unload(Window *window) {
  menu_layer_destroy(s_route_menu);
}

static void init(void) {
  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = main_window_load,
    .unload = main_window_unload
  });
	window_stack_push(s_window, true);
	
  //window_set_click_config_provider(s_window, (ClickConfigProvider) config_provider);
  
	// Register AppMessage handlers
	app_message_register_inbox_received(in_received_handler); 
	app_message_register_inbox_dropped(in_dropped_handler); 
	app_message_register_outbox_failed(out_failed_handler);

  // Initialize AppMessage inbox and outbox buffers with a suitable size
  int inbox_size = APP_MESSAGE_INBOX_SIZE_MINIMUM;
  int outbox_size = APP_MESSAGE_OUTBOX_SIZE_MINIMUM;
  app_message_open(inbox_size, outbox_size);
}

static void deinit(void) {
	app_message_deregister_callbacks();
	window_destroy(s_window);

  //TODO: free(name);
  //TODO: free(short_name);
}

int main( void ) {
	init();
	app_event_loop();
	deinit();
}