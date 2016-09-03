#include <pebble.h>

#define ROUTES_LEN 24
#define PATTERN_LEN 256
#define SECTIONS_LEN 4
#define INBOX_SIZE APP_MESSAGE_INBOX_SIZE_MINIMUM
#define OUTBOX_SIZE APP_MESSAGE_OUTBOX_SIZE_MINIMUM

enum {
  ROUTE_ON_CAMPUS = 0,
  ROUTE_OFF_CAMPUS = 1,
  ROUTE_GAME_DAY = 2,
  ROUTE_OTHER = 3
};

enum {
  STOP_WAYPOINT = 0,
  STOP_UNTIMED = 1,
  STOP_TIMED = 2
};

enum {
  MESSAGE_STATUS = 0,
  MESSAGE_SET_INBOX_SIZE = 1,
  MESSAGE_ROUTES = 2,
  MESSAGE_ROUTE_PATTERN = 3
};

// A doubly linked list of bus stops
typedef struct StopNode{
  char *name;
  bool is_timed;
  GPoint *point;
  struct StopNode *next_stop;
  struct StopNode *prev_stop;
} StopNode;

// An array of points and a linked list of stops
typedef struct {
  uint16_t points_len;
  GPoint points[PATTERN_LEN];
  StopNode *stops_head;
} Pattern;

typedef struct{
  char *title;
  char *subtitle;
  uint8_t color_rgb[3];
  Pattern *pattern;
} MenuItem;

// Menu variables
static Window *s_menu_window = NULL;
static MenuLayer *s_menu_layer = NULL;
static TextLayer *s_menu_loading_text = NULL;
static GRect s_menu_loading_frame;
static int s_section_lens[SECTIONS_LEN] = {0, 0, 0, 0};
static char *s_section_titles[SECTIONS_LEN] = {"On Campus", "Off Campus", "Game Day", "Other"};
static MenuItem s_menu_items[SECTIONS_LEN][ROUTES_LEN];
static bool s_menu_loading = S_FALSE;

// Route variables
static Window *s_route_window = NULL;
static TextLayer *s_route_name_text = NULL;
static GRect s_route_name_frame;
static MenuItem *s_selected_route = NULL;

//========================================= UTILITY ======================================================
void print_stops_list(StopNode *head){
  StopNode *curr = head;
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Linked stop list follows");
  do {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "  Stop: %s", curr->name);
    curr = curr->next_stop;
  } while(curr != head && curr != NULL);
}

// Insert stop into the circular doubly linked list
void insert_stop(Pattern* pattern, StopNode* stop){
  // Empty list
  if(pattern->stops_head == NULL){
    pattern->stops_head = stop;
  }
  else{
    StopNode *prev = pattern->stops_head->prev_stop;
    // Single item list
    if(prev == NULL){
      pattern->stops_head->next_stop = stop;
    }
    // List lenght > 1
    else{
      prev->next_stop = stop;
    }
    pattern->stops_head->prev_stop = stop;
  }
}

// Destroy the list
void destroy_stop_list(StopNode* head){
  if(head != NULL){
    StopNode *curr = head;
    do {
      if(strlen(curr->name) > 0) free(curr->name);
      StopNode *prev = curr;
      curr = curr->next_stop;
      free(prev);
    } while(curr != head && curr != NULL);
  }  
}

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
	dict_write_uint8(iter, MESSAGE_KEY_message_type, MESSAGE_SET_INBOX_SIZE);
  dict_write_uint16(iter, MESSAGE_KEY_inbox_size, INBOX_SIZE);
	
	dict_write_end(iter);
  app_message_outbox_send();
}

// Write message to buffer & send
static void request_routes(){
	DictionaryIterator *iter;
	
	app_message_outbox_begin(&iter);
	dict_write_uint8(iter, MESSAGE_KEY_message_type, MESSAGE_ROUTES);
	
	dict_write_end(iter);
  app_message_outbox_send();
}

// Request the info to populate the route menu
static void request_route_pattern(const char *short_name){
	DictionaryIterator *iter;
	
	app_message_outbox_begin(&iter);
	dict_write_uint8(iter, MESSAGE_KEY_message_type, MESSAGE_ROUTE_PATTERN);
  dict_write_cstring(iter, MESSAGE_KEY_route_short_name, short_name);
	
	dict_write_end(iter);
  app_message_outbox_send();
}

// Called when PebbleKitJS does not acknowledge receipt of a message
static void out_failed_handler(DictionaryIterator *failed, AppMessageResult reason, void *context) {
}

//========================================= INBOX HANDLING ======================================================
static void status_msg_handler(DictionaryIterator *received, void *context) {
  Tuple *tuple;
  
  tuple = dict_find(received, MESSAGE_KEY_js_status);
  if(tuple) {
    uint32_t js_status = (int)tuple->value->uint32;
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Received status notification: %d", (int)js_status); 
    if(js_status == 1) send_inbox_size();
    else if(js_status == 0) request_routes();
  }
}

static void routes_msg_handler(DictionaryIterator *received, void *context){
  Tuple *tuple;
  
  // Save the route name on the heap for use in the menu
  char *name = "\0"; 
  tuple = dict_find(received, MESSAGE_KEY_route_name);
  if(tuple){
    name = (char*)malloc(strlen(tuple->value->cstring)+1);
    strcpy(name, tuple->value->cstring);
  }

  char *short_name = "\0"; 
  tuple = dict_find(received, MESSAGE_KEY_route_short_name);
  if(tuple){
    short_name = (char*)malloc(strlen(tuple->value->cstring)+1);
    strcpy(short_name, tuple->value->cstring);
  }

  uint8_t color_r = 0;
  tuple = dict_find(received, MESSAGE_KEY_route_color_r);
  if(tuple){
    color_r = tuple->value->uint8;
  }

  uint8_t color_g = 0;
  tuple = dict_find(received, MESSAGE_KEY_route_color_g);
  if(tuple){
    color_g = tuple->value->uint8;
  }

  uint8_t color_b = 0;
  tuple = dict_find(received, MESSAGE_KEY_route_color_b);
  if(tuple){
    color_b = tuple->value->uint8;
  }

  int group = ROUTE_OTHER;
  tuple = dict_find(received, MESSAGE_KEY_route_type);
  if(tuple){
    group = tuple->value->uint8;
  }
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Received route: %s - %s : group %d : rgb(%d, %d, %d)", short_name, name, group, color_r, color_g, color_b);

  MenuItem *newItem = &s_menu_items[group][s_section_lens[group]];
  newItem->title = short_name;
  newItem->subtitle = name;
  newItem->color_rgb[0] = color_r;
  newItem->color_rgb[1] = color_g;
  newItem->color_rgb[2] = color_b;
  newItem->pattern = NULL;
  menu_layer_set_selected_index(s_menu_layer, MenuIndex(group, s_section_lens[group]), MenuRowAlignCenter, false);
  s_section_lens[group]++;

  // Show the route menu/hide the loading message
  if(s_menu_loading){
    s_menu_loading = S_FALSE;
    layer_set_hidden(menu_layer_get_layer(s_menu_layer), false);
    layer_set_hidden(text_layer_get_layer(s_menu_loading_text), true);
  }

  layer_mark_dirty(menu_layer_get_layer(s_menu_layer));
}

static void route_pattern_msg_handler(DictionaryIterator *received, void *context) {
  Tuple *tuple;
  
  uint16_t list_len = 0; 
  tuple = dict_find(received, MESSAGE_KEY_list_len);
  if(tuple){
    list_len = tuple->value->uint16;
  }
  
  uint8_t point_type = STOP_WAYPOINT; 
  tuple = dict_find(received, MESSAGE_KEY_stop_type);
  if(tuple){
    point_type = tuple->value->uint8;
  }
  
  char *name = "\0";
  if(point_type == STOP_TIMED || point_type == STOP_UNTIMED){
    tuple = dict_find(received, MESSAGE_KEY_stop_name);
    if(tuple){
      name = (char*)malloc(strlen(tuple->value->cstring)+1);
      strcpy(name, tuple->value->cstring);
    }
  }
  
  int32_t point_x = 0; 
  tuple = dict_find(received, MESSAGE_KEY_stop_x);
  if(tuple){
    point_x = tuple->value->int32;
  }
  
  int32_t point_y = 0; 
  tuple = dict_find(received, MESSAGE_KEY_stop_y);
  if(tuple){
    point_y = tuple->value->int32;
  }
  
  // Dont store short name on heap here
  char *short_name = "ERROR"; 
  tuple = dict_find(received, MESSAGE_KEY_route_short_name);
  if(tuple){
    short_name = tuple->value->cstring;
  }
  
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Received pattern point: %s : type %d : (%d, %d) : route %s : list_len %d", name, (int)point_type, (int)point_x, (int)point_y, short_name, (int)list_len);
  
  // A quick search for the route index. Easier than passing it over the wire
  MenuItem *route = NULL;
  for(int i=0; i<SECTIONS_LEN; i++){
    bool broke = false;
    for(int j=0; j<s_section_lens[i]; j++){
      if(strcmp(short_name, s_menu_items[i][j].title) == 0){
        route = &s_menu_items[i][j];
        broke = true;
        break;
      }
      if(broke) break;
    }
  }
  if(route != NULL){
    if(route->pattern == NULL){
      route->pattern = (Pattern*)malloc(sizeof(Pattern));
      route->pattern->points_len = 0;
      route->pattern->stops_head = NULL;
    }
    route->pattern->points[route->pattern->points_len] = GPoint(point_x, point_y);
    if(point_type == STOP_TIMED || point_type == STOP_UNTIMED){
      StopNode *stop = (StopNode*)malloc(sizeof(StopNode));
      stop->name = name;
      stop->is_timed = point_type == STOP_TIMED;
      stop->point = &(route->pattern->points[route->pattern->points_len]);
      insert_stop(route->pattern, stop);
      print_stops_list(route->pattern->stops_head);
    }
    route->pattern->points_len++;
  }
  else{
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Pattern references non-existance route: %s", short_name);
  }
}
  
// Called when a message is received from PebbleKitJS
static void in_received_handler(DictionaryIterator *received, void *context) {
	Tuple *tuple;
  
	tuple = dict_find(received, MESSAGE_KEY_message_type);
	if(tuple) {
    uint8_t msg_type = tuple->value->uint8;
    switch(msg_type){
      case MESSAGE_STATUS : 
        status_msg_handler(received, context);
      break;
      
      case MESSAGE_ROUTES :
        routes_msg_handler(received, context);
      break;
      
      case MESSAGE_ROUTE_PATTERN :
        route_pattern_msg_handler(received, context);
      break;
      
      default :
        APP_LOG(APP_LOG_LEVEL_DEBUG, "Recieved a message of unexpected type: %d", msg_type);
      break;
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
  MenuItem *item = &s_menu_items[i][j];
  #ifdef PBL_COLOR
  if (menu_layer_is_index_selected(s_menu_layer, cell_index)) {
    menu_layer_set_highlight_colors(s_menu_layer, GColorFromRGB(item->color_rgb[0], item->color_rgb[1], item->color_rgb[2]), GColorWhite);
  }
  #endif
  menu_cell_basic_draw(gctx, cell_layer, item->title, item->subtitle, NULL);
}

static void enter_route_window(); // Defined in route window functions
static void menu_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *context) {
  uint16_t i = cell_index->section;
  uint16_t j = cell_index->row;
  s_selected_route = &s_menu_items[i][j];
  enter_route_window();
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
  GRect window_frame = layer_get_frame(window_layer);
    
  // Create the loading text
  s_menu_loading_text = text_layer_create(window_frame);
  text_layer_set_background_color(s_menu_loading_text, GColorClear);
  text_layer_set_text_color(s_menu_loading_text, GColorBlack);
  text_layer_set_text_alignment(s_menu_loading_text, GTextAlignmentCenter);
  text_layer_set_text(s_menu_loading_text, "Loading routes...");
  layer_add_child(window_layer, text_layer_get_layer(s_menu_loading_text));
  
  // Center the loading text
  GSize loading_text_size = text_layer_get_content_size(s_menu_loading_text);
  s_menu_loading_frame = window_frame;
  s_menu_loading_frame.origin.y = (window_frame.size.h - loading_text_size.h)/2;
  
  // Move the loading text to its new position
  layer_set_frame(text_layer_get_layer(s_menu_loading_text), s_menu_loading_frame);
  layer_mark_dirty(text_layer_get_layer(s_menu_loading_text));
    
  s_menu_layer = menu_layer_create(window_frame);
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
  text_layer_destroy(s_menu_loading_text);
}

//========================================= ROUTE WINDOW ======================================================
static void route_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect window_frame = layer_get_frame(window_layer);
  
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Loading route window"); 
  if(s_selected_route->pattern == NULL) request_route_pattern(s_selected_route->title);
  
  // Create the route name text
  s_route_name_text = text_layer_create(window_frame);
  text_layer_set_background_color(s_route_name_text, GColorClear);
  text_layer_set_text_color(s_route_name_text, GColorBlack);
  text_layer_set_text_alignment(s_route_name_text, GTextAlignmentCenter);
  if(s_selected_route){
    text_layer_set_text(s_route_name_text, s_selected_route->subtitle);
  }
  else{
    text_layer_set_text(s_route_name_text, "No route selected");
  }
  layer_add_child(window_layer, text_layer_get_layer(s_route_name_text));
  
  // Center the loading text
  GSize route_name_text_size = text_layer_get_content_size(s_route_name_text);
  s_route_name_frame = window_frame;
  s_route_name_frame.origin.y = (window_frame.size.h - route_name_text_size.h)/2;
  
  // Move the loading text to its new position
  layer_set_frame(text_layer_get_layer(s_route_name_text), s_route_name_frame);
  layer_mark_dirty(text_layer_get_layer(s_route_name_text));
}

static void route_window_unload(Window *window) {
  text_layer_destroy(s_route_name_text);
}

static void enter_route_window(){
  if(s_route_window == NULL){
    s_route_window = window_create();
    window_set_window_handlers(s_route_window, (WindowHandlers) {
      .load = route_window_load,
      .unload = route_window_unload
    });
  }
	window_stack_push(s_route_window, true);
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

// Free up the heap memory used by menu item titles and petterns
static void destroy_menu_items(){
  for(int i=0; i<SECTIONS_LEN; i++){
    for(int j=0; j<s_section_lens[i]; j++){
      MenuItem *item = &s_menu_items[i][j];
      if(strlen(item->title) > 0) free(item->title);
      if(strlen(item->subtitle) > 0) free(item->subtitle);
      destroy_stop_list(item->pattern->stops_head);
      free(item->pattern);
    }
    s_section_lens[i] = 0;
  }
}

static void deinit(void) {
	app_message_deregister_callbacks();
	window_destroy(s_menu_window);
  window_destroy(s_route_window);
  
  destroy_menu_items();
  
  // Free up heap memory used by stop listings
}

int main( void ) {
	init();
	app_event_loop();
	deinit();
}