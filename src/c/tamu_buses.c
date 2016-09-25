#include <pebble.h>

#define PATTERN_LEN 256
#define STOPS_LEN 32
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
  MESSAGE_STATUS = 0,
  MESSAGE_SET_INBOX_SIZE = 1,
  MESSAGE_ROUTES = 2,
  MESSAGE_ROUTE_PATTERN = 3,
  MESSAGE_ROUTE_PATTERN_POINTS = 4,
  MESSAGE_ROUTE_PATTERN_STOPS = 5
};

typedef struct {
  GPoint **points;
  uint16_t points_len;
} ConvexHull;

// A doubly linked list of bus stops
typedef struct{
  char *name;
  bool is_timed;
  GPoint *point;
} Stop;

// An array of points and a linked list of stops
typedef struct {
  uint16_t points_len;
  uint16_t stops_len;
  GPoint *points;
  Stop *stops;
  ConvexHull *convex_hull;
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
static MenuItem *s_menu_items[SECTIONS_LEN] = {NULL, NULL, NULL, NULL};
static bool s_menu_loading = S_FALSE;

// Route variables
static Window *s_route_window = NULL;
static TextLayer *s_route_name_text = NULL;
static Layer *s_route_pattern = NULL;
static GPath *s_pattern_path = NULL;
static GRect s_route_name_frame;
static MenuItem *s_selected_route = NULL;
static bool s_pattern_loading = S_FALSE;
static GPathInfo* s_pattern_gpath_info = NULL;
static GPath* s_pattern_gpath = NULL;
static bool s_pattern_updated = S_FALSE;

//========================================= COMPUTATIONAL GEOMETRY :D ======================================================

// Taken from StackOverflow user @Craig McQueen 
// http://stackoverflow.com/questions/1100090/looking-for-an-efficient-integer-square-root-algorithm-for-arm-thumb2
uint32_t pebble_sqrt(uint32_t a_nInput)
{
    uint32_t op  = a_nInput;
    uint32_t res = 0;
    uint32_t one = 1uL << 30; // The second-to-top bit is set: use 1u << 14 for uint16_t type; use 1uL<<30 for uint32_t type
  
    // "one" starts at the highest power of four <= than the argument.
    while (one > op)
    {
        one >>= 2;
    }

    while (one != 0)
    {
        if (op >= res + one)
        {
            op = op - (res + one);
            res = res +  2 * one;
        }
        res >>= 1;
        one >>= 2;
    }
    return res;
}

static GPoint center(GPoint* p, GPoint* q){
  uint16_t avg_x = (p->x + q->x)/2;
  uint16_t avg_y = (p->y + q->y)/2;
  return GPoint(avg_x, avg_y);
}

static uint32_t distance(GPoint* p, GPoint* q){
  return pebble_sqrt((p->x - q->x) * (p->x - q->x) + (p->y - q->y) *(p->y - q->y));
}

// Finds the left tangent of the line through a point against a counter-clockwise sorted convex hull
static GPoint* left_tangent(GPoint* p, ConvexHull* chull){
  return NULL;
}

// Finds the right tangent of the line through a point against a counter-clockwise sorted convex hull
static GPoint* right_tangent(GPoint* p, ConvexHull* chull){
  return NULL;
}

// If the point is external, make it part of the convex hull. If it is internal, do nothing
static void integrate_point(GPoint* p, ConvexHull* chull){
  // Be stupid and assume all points are in the convex hull
  if(p != NULL){
    chull->points[chull->points_len] = p;
    chull->points_len++;
  }
}

// Returns an array of two points, being the points which are farthest from each other
// Currently O(n^2); Can be reduced to O(n) with rotating calipers algorithm
// Extremes needs to be a GPoint* pair to be filled
static void extreme_points(ConvexHull* chull, GPoint** extremes){
  APP_LOG(APP_LOG_LEVEL_DEBUG, "1");
  extremes[0] = NULL;
  extremes[1] = NULL;
  
  APP_LOG(APP_LOG_LEVEL_DEBUG, "2");
  if(chull->points_len >= 1){
    extremes[0] = chull->points[0];
  }
  if(chull->points_len >= 2){
    extremes[1] = chull->points[1];
  }
  
  APP_LOG(APP_LOG_LEVEL_DEBUG, "3");
  if(chull->points_len > 2){
    uint16_t max_dist = 0;
    for(int i=0; i<chull->points_len; ++i){
      for(int j=i+1; j<chull->points_len; ++j){
        uint32_t dist = distance(chull->points[i], chull->points[j]);
        if(dist > max_dist){
          extremes[0] = chull->points[i];
          extremes[1] = chull->points[j];
          max_dist = dist;
        }
      }
    }
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Found extreme points to be (%d, %d) and (%d, %d) with a dist of %d", extremes[0]->x, extremes[0]->y, extremes[1]->x, extremes[1]->y, max_dist);
  }
}

//========================================= CLEAN UP FUNCTIONS ======================================================

static void destroy_convex_hull(ConvexHull* chull){
  if(chull != NULL){
    if(chull->points != NULL){
      free(chull->points);
      chull->points = NULL;
    }
    chull->points_len = 0;
  } 
}

static void destroy_pattern_points(Pattern* pattern){
  if(pattern != NULL){
    free(pattern->points);
    pattern->points = NULL;
  } 
  pattern->points_len = 0;
}

static void destroy_pattern_stops(Pattern* pattern){
  if(pattern != NULL){
    if(pattern->stops != NULL){
      for(int i=0; i<pattern->stops_len; i++){
        if(strlen(pattern->stops[i].name) > 0) free(pattern->stops[i].name);
      }
      free(pattern->stops);
    }
    pattern->stops_len = 0;
  } 
}
  
// Free up the heap memory used by menu item titles and patterns
static void destroy_menu_items(){
  for(int i=0; i<SECTIONS_LEN; i++){
    for(int j=0; j<s_section_lens[i]; j++){
      MenuItem *item = &s_menu_items[i][j];
      if(strlen(item->title) > 0) free(item->title);
      if(strlen(item->subtitle) > 0) free(item->subtitle);
      destroy_pattern_points(item->pattern);
      destroy_pattern_stops(item->pattern);
      destroy_convex_hull(item->pattern->convex_hull);
      free(item->pattern);
    }
    s_section_lens[i] = 0;
    free(s_menu_items[i]);
    s_menu_items[i] = NULL;
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
  
  uint32_t index = 0;
  tuple = dict_find(received, MESSAGE_KEY_list_index);
  if(tuple){
    index = tuple->value->uint32;
  }
  
  uint32_t list_len = 0;
  tuple = dict_find(received, MESSAGE_KEY_list_len);
  if(tuple){
    list_len = tuple->value->uint32;
  }
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Received route: %s - %s : group %d : rgb(%d, %d, %d) : %d of %d", short_name, name, group, color_r, color_g, color_b, (int)index+1, (int)list_len);

  if(list_len > 0){
    // This must be new list
    s_menu_items[group] = (MenuItem*)malloc(sizeof(MenuItem) * list_len);
  }
  
  MenuItem *new_item = &s_menu_items[group][index];
  new_item->title = short_name;
  new_item->subtitle = name;
  new_item->color_rgb[0] = color_r;
  new_item->color_rgb[1] = color_g;
  new_item->color_rgb[2] = color_b;
  new_item->pattern = (Pattern*)malloc(sizeof(Pattern));
  new_item->pattern->points_len = 0;
  new_item->pattern->points = NULL;
  new_item->pattern->stops_len = 0;
  new_item->pattern->stops = NULL;
  new_item->pattern->convex_hull = NULL;
  s_section_lens[group]++;

  // Show the route menu/hide the loading message
  if(s_menu_loading){
    s_menu_loading = S_FALSE;
    layer_set_hidden(menu_layer_get_layer(s_menu_layer), false);
    layer_set_hidden(text_layer_get_layer(s_menu_loading_text), true);
    
    // Fix the selection so it points properly to the first item
    menu_layer_set_selected_index(s_menu_layer, MenuIndex(0,0), MenuRowAlignCenter, true);
    menu_layer_reload_data(s_menu_layer);
  }

  layer_mark_dirty(menu_layer_get_layer(s_menu_layer));
}

static void route_pattern_points_msg_handler(DictionaryIterator *received, void *context) {
  Tuple *tuple;
  
  int32_t point_x = 0; 
  tuple = dict_find(received, MESSAGE_KEY_point_x);
  if(tuple){
    point_x = tuple->value->int32;
  }
  
  int32_t point_y = 0; 
  tuple = dict_find(received, MESSAGE_KEY_point_y);
  if(tuple){
    point_y = tuple->value->int32;
  }
  
  uint32_t index = 0; 
  tuple = dict_find(received, MESSAGE_KEY_list_index);
  if(tuple){
    index = tuple->value->uint32;
  }
  
  uint32_t list_len = 0; 
  tuple = dict_find(received, MESSAGE_KEY_list_len);
  if(tuple){
    list_len = tuple->value->uint32;
  }
  
  // Dont store short name on heap here
  char *route_name = "ERROR"; 
  tuple = dict_find(received, MESSAGE_KEY_route_short_name);
  if(tuple){
    route_name = tuple->value->cstring;
  }
  
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Received pattern point: (%d, %d) : route %s : %d of %d", (int)point_x, (int)point_y, route_name, (int)index+1, (int)list_len);
  
  // A quick search for the route index. Easier than passing it over the wire 
  // TODO: On second thought this sucks. :P
  MenuItem *route = NULL;
  for(int i=0; i<SECTIONS_LEN; i++){
    bool broke = false;
    for(int j=0; j<s_section_lens[i]; j++){
      if(strcmp(route_name, s_menu_items[i][j].title) == 0){
        route = &s_menu_items[i][j];
        broke = true;
        break;
      }
      if(broke) break;
    }
  }
  if(route != NULL){
    if(route->pattern->points == NULL){
      // This is a new list transmission
      //destroy_pattern_points(route->pattern);
      route->pattern->points = (GPoint*)malloc(sizeof(GPoint) * list_len);
      route->pattern->points_len = 0;
    }
    
    if(route->pattern->convex_hull == NULL){
      route->pattern->convex_hull = (ConvexHull*)malloc(sizeof(ConvexHull));
      route->pattern->convex_hull->points = (GPoint**)malloc(sizeof(GPoint*) * list_len); //Worst case convex hull contains all points
      route->pattern->convex_hull->points_len = 0;
    }
    route->pattern->points[index] = GPoint(point_x, point_y);
    route->pattern->points_len++;
    
    integrate_point(&route->pattern->points[index], route->pattern->convex_hull);
    s_pattern_updated = S_TRUE;
    layer_mark_dirty(s_route_pattern);
  }
  else{
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Pattern references non-existance route: %s", route_name);
  }
  
  if(s_pattern_loading){
    s_pattern_loading = S_FALSE;
    layer_set_hidden(s_route_pattern, false);
    layer_set_hidden(text_layer_get_layer(s_route_name_text), true);
  }
}

static void route_pattern_stops_msg_handler(DictionaryIterator *received, void *context) {
  Tuple *tuple;
  
  // Save the route name on the heap for use in the menu
  char *stop_name = "\0"; 
  tuple = dict_find(received, MESSAGE_KEY_stop_name);
  if(tuple){
    stop_name = (char*)malloc(strlen(tuple->value->cstring)+1);
    strcpy(stop_name, tuple->value->cstring);
  }
  
  bool is_timed = false; 
  tuple = dict_find(received, MESSAGE_KEY_stop_is_timed);
  if(tuple){
    is_timed = tuple->value->uint8;
  }
  
  uint32_t stop_point_index = 0;
  tuple = dict_find(received, MESSAGE_KEY_stop_point_index);
  if(tuple){
    stop_point_index = tuple->value->uint32;
  }
  
  uint32_t index = 0; 
  tuple = dict_find(received, MESSAGE_KEY_list_index);
  if(tuple){
    index = tuple->value->uint32;
  }
  
  uint32_t list_len = 0; 
  tuple = dict_find(received, MESSAGE_KEY_list_len);
  if(tuple){
    list_len = tuple->value->uint32;
  }
  
  // Dont store short name on heap here
  char *route_name = "ERROR"; 
  tuple = dict_find(received, MESSAGE_KEY_route_short_name);
  if(tuple){
    route_name = tuple->value->cstring;
  }
  
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Received pattern stop: %s : timed %d : ->%d : route %s : %d of %d", stop_name, (int)is_timed, (int)stop_point_index, route_name, (int)index+1, (int)list_len);
  
  // A quick search for the route index. Easier than passing it over the wire 
  // TODO: On second thought this sucks. :P
  MenuItem *route = NULL;
  for(int i=0; i<SECTIONS_LEN; i++){
    bool broke = false;
    for(int j=0; j<s_section_lens[i]; j++){
      if(strcmp(route_name, s_menu_items[i][j].title) == 0){
        route = &s_menu_items[i][j];
        broke = true;
        break;
      }
      if(broke) break;
    }
  }
  if(route != NULL){
    if(route->pattern->points == NULL){
      // This is a new list transmission
      //destroy_pattern_points(route->pattern);
      route->pattern->points = (GPoint*)malloc(sizeof(GPoint) * list_len);
      route->pattern->points_len = 0;
    }
    if(route->pattern->stops == NULL){
      // This is a new list transmission
      //destroy_pattern_stops(route->pattern);
      route->pattern->stops = (Stop*)malloc(sizeof(Stop) * list_len);
      route->pattern->stops_len = 0;
    }
    route->pattern->stops[index].is_timed = is_timed;
    route->pattern->stops[index].name = stop_name;
    route->pattern->stops[index].point = route->pattern->points + stop_point_index;
    route->pattern->stops_len++;
  }
  else{
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Pattern references non-existance route: %s", route_name);
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
      
      case MESSAGE_ROUTE_PATTERN_POINTS :
        route_pattern_points_msg_handler(received, context);
      break;
      
      case MESSAGE_ROUTE_PATTERN_STOPS :
        route_pattern_stops_msg_handler(received, context);
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
static void pattern_layer_update_proc(Layer *my_layer, GContext* ctx){
  if(s_pattern_updated && s_selected_route != NULL){
    if(s_selected_route->pattern != NULL && s_selected_route->pattern->points != NULL){
      s_pattern_gpath_info = malloc(sizeof(GPathInfo));
      s_pattern_gpath_info->num_points = s_selected_route->pattern->points_len;
      
      // Create extremes of the convex hull
      GPoint* hull_extremes[2];
      extreme_points(s_selected_route->pattern->convex_hull, hull_extremes);
      
      if(hull_extremes[0] != NULL && hull_extremes[1] != NULL){         
        
        // Get the scale
        GRect pattern_frame = layer_get_frame(my_layer);
        uint32_t extreme_dist = distance(hull_extremes[0], hull_extremes[1]);
        double scale_factor = ((double)pattern_frame.size.w) / extreme_dist;
        
        // Get the center offset
        GPoint hull_center = center(hull_extremes[0], hull_extremes[1]);
        GPoint frame_center = grect_center_point(&pattern_frame);
        GPoint center_offset = GPoint(frame_center.x/scale_factor - hull_center.x, frame_center.y/scale_factor - hull_center.y);
          
        s_pattern_gpath_info->points = malloc(sizeof(GPoint)*s_pattern_gpath_info->num_points);
        for(uint16_t i=0; i<s_pattern_gpath_info->num_points; ++i){
          GPoint scaled_point = s_selected_route->pattern->points[i];
          scaled_point.x += center_offset.x;
          scaled_point.y += center_offset.y;
          scaled_point.x *= scale_factor;
          scaled_point.y *= scale_factor;
          
          s_pattern_gpath_info->points[i] = scaled_point;
          APP_LOG(APP_LOG_LEVEL_DEBUG, "Scaled Point: (%d, %d)", scaled_point.x, scaled_point.y);
        }      
        s_pattern_path = gpath_create(s_pattern_gpath_info);
      }
    }
    s_pattern_updated = S_FALSE;
  }
  if(s_pattern_gpath != NULL){
    // Fill the path:
    graphics_context_set_fill_color(ctx, GColorWhite);
    gpath_draw_filled(ctx, s_pattern_gpath);
    // Stroke the path:
    graphics_context_set_stroke_color(ctx, GColorBlack);
    gpath_draw_outline(ctx, s_pattern_gpath);
  }
}

static void route_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect window_frame = layer_get_frame(window_layer);
  
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Loading route window"); 
  if(s_selected_route->pattern->points == NULL && s_selected_route->pattern->stops == NULL) request_route_pattern(s_selected_route->title);
  
  // Create the route name text
  s_route_name_text = text_layer_create(window_frame);
  text_layer_set_background_color(s_route_name_text, GColorClear);
  text_layer_set_text_color(s_route_name_text, GColorBlack);
  text_layer_set_text_alignment(s_route_name_text, GTextAlignmentCenter);
  if(s_selected_route != NULL){
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
  
  // Create the pattern display layer
  s_route_pattern = layer_create(window_frame);
  layer_set_hidden(s_route_pattern, true);
  s_pattern_loading = S_TRUE;
  layer_set_update_proc(s_route_pattern, pattern_layer_update_proc);
  layer_add_child(window_layer, s_route_pattern);
}

static void route_window_unload(Window *window) {
  text_layer_destroy(s_route_name_text);
  if(s_pattern_gpath_info != NULL){
    free(s_pattern_gpath_info->points);
    free(s_pattern_gpath_info);
    s_pattern_gpath_info = NULL;
  }
  if(s_pattern_gpath != NULL){
    gpath_destroy(s_pattern_gpath);
    free(s_pattern_gpath);
    s_pattern_gpath = NULL;
  }
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