#include <pebble.h>

#define ROUTES_LEN 24
#define SECTIONS_LEN 4
#define ROUTE_SHORT_NAME_LEN 8
#define ROUTE_NAME_LEN 48
#define INBOX_SIZE APP_MESSAGE_INBOX_SIZE_MINIMUM
#define OUTBOX_SIZE APP_MESSAGE_OUTBOX_SIZE_MINIMUM

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

static Window *s_menu_window;
static MenuLayer *s_menu_layer;
static TextLayer *s_menu_loading_text;

static GRect s_menu_window_frame;
static GRect s_menu_loading_frame;

static int s_section_lens[SECTIONS_LEN] = {0, 0, 0, 0};
static char *s_section_titles[SECTIONS_LEN] = {"On Campus", "Off Campus", "Game Day", "Other"};
static MenuItem s_menu_items[SECTIONS_LEN][ROUTES_LEN];
static bool s_menu_loading = S_FALSE;

//========================================= CLICK HANDLING ======================================================
void down_single_click_handler(ClickRecognizerRef recognizer, void *context) {
}

void config_provider(Window *window) {
  window_single_click_subscribe(BUTTON_ID_SELECT, down_single_click_handler);
}

//========================================= OUTBOX HANDLING ======================================================
// Write message to buffer & send
static void send_inbox_size(){
	DictionaryIterator *iter;
	
	app_message_outbox_begin(&iter);
	dict_write_cstring(iter, MESSAGE_KEY_request, "SET_INBOX_SIZE");
  dict_write_uint16(iter, MESSAGE_KEY_inbox_size, INBOX_SIZE);
	
	dict_write_end(iter);
  app_message_outbox_send();
}

// Write message to buffer & send
static void request_routes(){
	DictionaryIterator *iter;
	
	app_message_outbox_begin(&iter);
	dict_write_cstring(iter, MESSAGE_KEY_request, "ROUTES");
	
	dict_write_end(iter);
  app_message_outbox_send();
}

// Request the info to populate the route menu
static void request_route_info(const char *short_name){
	DictionaryIterator *iter;
	
	app_message_outbox_begin(&iter);
	dict_write_cstring(iter, MESSAGE_KEY_request, "ROUTE_INFO");
  dict_write_cstring(iter, MESSAGE_KEY_route_short_name, short_name);
	
	dict_write_end(iter);
  app_message_outbox_send();
}

// Called when PebbleKitJS does not acknowledge receipt of a message
static void out_failed_handler(DictionaryIterator *failed, AppMessageResult reason, void *context) {
}

//========================================= INBOX HANDLING ======================================================
// Called when a message is received from PebbleKitJS
static void in_received_handler(DictionaryIterator *received, void *context) {
	Tuple *tuple;
	
  // Is it the ready notification?
	tuple = dict_find(received, MESSAGE_KEY_jsStatus);
	if(tuple) {
    uint32_t js_status = (int)tuple->value->uint32;
		APP_LOG(APP_LOG_LEVEL_DEBUG, "Received status notification: %d", (int)js_status); 
    if(js_status == 1) send_inbox_size();
    else if(js_status == 0) request_routes();
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
      short_name[0] = '\0';
      tuple = dict_find(received, MESSAGE_KEY_route_short_name);
      if(tuple){
        strcpy(short_name, tuple->value->cstring);
      }
      
      char *group = "\0";
      tuple = dict_find(received, MESSAGE_KEY_route_group);
      if(tuple){
        group = tuple->value->cstring;
      }
  		APP_LOG(APP_LOG_LEVEL_DEBUG, "Received route: %s - %s : %s", short_name, name, group);
      
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
      
      s_menu_items[i][s_section_lens[i]].title = short_name;
      s_menu_items[i][s_section_lens[i]].subtitle = name;
      menu_layer_set_selected_index(s_menu_layer, MenuIndex(i, s_section_lens[i]), MenuRowAlignCenter, false);
      s_section_lens[i]++;
      
      // Show the route menu/hide the loading message
      if(s_menu_loading){
        s_menu_loading = S_FALSE;
        layer_set_hidden(menu_layer_get_layer(s_menu_layer), false);
        layer_set_hidden(text_layer_get_layer(s_menu_loading_text), true);
      }
      
      layer_mark_dirty(menu_layer_get_layer(s_menu_layer));
    }
	}
}

// Called when an incoming message from PebbleKitJS is dropped
static void in_dropped_handler(AppMessageResult reason, void *context) {	
}

//========================================= MENU CALLBACKS ======================================================
static uint16_t menu_get_num_sections_callback(MenuLayer *menu_layer, void *context){
  return SECTIONS_LEN;
}

static uint16_t menu_get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *context) {
  return s_section_lens[section_index];
}

static int16_t menu_get_header_height_callback(MenuLayer *menu_layer, uint16_t section_index, void *context) {
  return MENU_CELL_BASIC_HEADER_HEIGHT;
}

static void menu_draw_header_callback(GContext* gctx, const Layer *cell_layer, uint16_t section_index, void *context) {
  if(s_section_lens[section_index] > 0){
    menu_cell_basic_header_draw(gctx, cell_layer, s_section_titles[section_index]);
  }
}

static void menu_draw_row_callback(GContext* gctx, const Layer *cell_layer, MenuIndex *cell_index, void *context) {
  uint16_t i = cell_index->section;
  uint16_t j = cell_index->row;
  menu_cell_basic_draw(gctx, cell_layer, s_menu_items[i][j].title, s_menu_items[i][j].subtitle, NULL);
}

static void menu_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *context) {
  uint16_t i = cell_index->section;
  uint16_t j = cell_index->row;
  MenuItem *item = &s_menu_items[i][j];
  if(item){
    item->subtitle = "SELECTED";
    layer_mark_dirty(menu_layer_get_layer(s_menu_layer));
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

//========================================= MENU WINDOW ======================================================
static void menu_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  s_menu_window_frame = layer_get_frame(window_layer);
    
  // Create the loading text
  s_menu_loading_text = text_layer_create(s_menu_window_frame);
  text_layer_set_background_color(s_menu_loading_text, GColorClear);
  text_layer_set_text_color(s_menu_loading_text, GColorBlack);
  text_layer_set_text_alignment(s_menu_loading_text, GTextAlignmentCenter);
  text_layer_set_text(s_menu_loading_text, "Loading routes...");
  layer_add_child(window_layer, text_layer_get_layer(s_menu_loading_text));
  
  // Center the loading text
  GSize loading_text_size = text_layer_get_content_size(s_menu_loading_text);
  s_menu_loading_frame = s_menu_window_frame;
  s_menu_loading_frame.origin.y = (s_menu_window_frame.size.h - loading_text_size.h)/2;
  
  // Move the loading text to its new position
  layer_set_frame(text_layer_get_layer(s_menu_loading_text), s_menu_loading_frame);
  layer_mark_dirty(text_layer_get_layer(s_menu_loading_text));
    
  s_menu_layer = menu_layer_create(s_menu_window_frame);
  menu_layer_set_callbacks(s_menu_layer, NULL, (MenuLayerCallbacks){
    .get_num_sections = menu_get_num_sections_callback,
    .get_num_rows = menu_get_num_rows_callback,
    .get_header_height = menu_get_header_height_callback,
    .draw_header = menu_draw_header_callback,
    .draw_row = menu_draw_row_callback,
    .select_click = menu_select_callback,
    .get_cell_height = PBL_IF_ROUND_ELSE(get_cell_height_callback, NULL),
  });
  
  // Bind the menu layer's click config provider to the window for interactivity
  menu_layer_set_click_config_onto_window(s_menu_layer, window);
  s_menu_loading = S_TRUE;
  layer_set_hidden(menu_layer_get_layer(s_menu_layer), true);
  layer_add_child(window_layer, menu_layer_get_layer(s_menu_layer));
}

static void menu_window_unload(Window *window) {
  menu_layer_destroy(s_menu_layer);
}

//========================================= ROUTE WINDOW ======================================================
static void route_window_load(Window *window) {
}

static void route_window_unload(Window *window) {
}

//========================================= INIT ======================================================
static void init(void) {
  s_menu_window = window_create();
  window_set_window_handlers(s_menu_window, (WindowHandlers) {
    .load = menu_window_load,
    .unload = menu_window_unload
  });
	window_stack_push(s_menu_window, true);
	
  //window_set_click_config_provider(s_window, (ClickConfigProvider) config_provider);
  
	// Register AppMessage handlers
	app_message_register_inbox_received(in_received_handler); 
	app_message_register_inbox_dropped(in_dropped_handler); 
	app_message_register_outbox_failed(out_failed_handler);

  // Initialize AppMessage inbox and outbox buffers with a suitable size
  int inbox_size = INBOX_SIZE;
  int outbox_size = OUTBOX_SIZE;
  app_message_open(inbox_size, outbox_size);
}

static void deinit(void) {
	app_message_deregister_callbacks();
	window_destroy(s_menu_window);
  
    // Free up the heap memory used by menu item titles
  for(int i=0; i<SECTIONS_LEN; i++){
    for(int j=0; j<s_section_lens[i]; j++){
      free(s_menu_items[i][j].title);
      free(s_menu_items[i][j].subtitle);
    }
    s_section_lens[i] = 0;
  }
}

int main( void ) {
	init();
	app_event_loop();
	deinit();
}