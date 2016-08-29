var keys = require('message_keys');
var parseCSSColor = require('./csscolorparser').parseCSSColor;

var GroupTypeEnum = {
  GROUP_ON_CAMPUS: 0,
  GROUP_OFF_CAMPUS: 1,
  GROUP_GAME_DAY: 2,
  GROUP_OTHER: 3
};

var StopTypeEnum = {
  STOP_WAYPOINT: 0,
  STOP_UNTIMED: 1,
  STOP_TIMED: 2
};

var apiUrl = "http://transport.tamu.edu/BusRoutesFeed/api/";
var routesPath = "Routes";
var patternPath = "route/{0}/pattern/{1}-{2}-{3}";
var myStatus = 1;

var retryWaitOriginal = 50;
var retryWait = retryWaitOriginal;
var pebbleInboxSize = 124; // The defult minimum
var pebbleUsedInbox = 0;

// String formating function 
// Courtesy of @fearphage http://stackoverflow.com/questions/610406/javascript-equivalent-to-printf-string-format/4673436#4673436
if (!String.prototype.format) {
  String.prototype.format = function() {
    var args = arguments;
    return this.replace(/{(\d+)}/g, function(match, number) { 
      return typeof args[number] != 'undefined' ? args[number] : match;
    });
  };
}

// Used to figure if there is room to send more items
// Courtesy of @tomwrong http://stackoverflow.com/questions/1248302/javascript-object-size
function roughSizeOfObject( object ) {

    var objectList = [];
    var stack = [ object ];
    var bytes = 0;

    while ( stack.length ) {
        var value = stack.pop();

        if ( typeof value === 'boolean' ) {
            bytes += 4;
        }
        else if ( typeof value === 'string' ) {
            bytes += value.length * 2;
        }
        else if ( typeof value === 'number' ) {
            bytes += 8;
        }
        else if
        (
            typeof value === 'object' && objectList.indexOf( value ) === -1
        )
        {
            objectList.push( value );

            for( var i in value ) {
                stack.push( value[ i ] );
            }
        }
    }
    return bytes;
}

// Function to send a message to the Pebble using AppMessage API
// We are currently only sending a message using the "status" appKey defined in appinfo.json/Settings     
function sendStatusMessage() {
	Pebble.sendAppMessage({"jsStatus": myStatus}, statusMessageSuccessHandler, statusMessageFailureHandler);
}

// Called when the message send attempt succeeds
function statusMessageSuccessHandler() {
  console.log("Ready message send succeeded.");  
}

// Called when the message send attempt fails
function statusMessageFailureHandler() {
  console.log("Ready message send failed.");
  sendStatusMessage();
}

// Called when JS is ready
Pebble.addEventListener("ready", function(e) {
  console.log("JS is ready!");
  sendStatusMessage();
});

// Used when sending a list of items
function sendNextItem(items, index) {
  // Send the message
  Pebble.sendAppMessage(items[index], function() {
    // Use success callback to increment index
    index++;
    retryWait = retryWaitOriginal;

    if(index < items.length) {
      sendNextItem(items, index);
    } else {
      console.log('Last item sent!');
    }
  }, function() {
    console.log('Item transmission failed at index: ' + index);
    // Retry with exponential backoff
    setTimeout(function(){ sendNextItem(items, index); }, retryWait);
    retryWait *= 2;
  });
}

// Send a list of items
function sendList(items) {
  var index = 0;
  sendNextItem(items, index);
}

// Called when incoming message from the Pebble is received
// We are currently only checking the "message" appKey defined in appinfo.json/Settings
Pebble.addEventListener("appmessage", function(e) {
  console.log("Received Message: " + e.payload.request);
  
  if(e.payload.request == "SET_INBOX_SIZE"){
    pebbleInboxSize = e.payload.inbox_size;
    console.log("Inbox size set to: " + pebbleInboxSize);
    myStatus = 0;
    sendStatusMessage();
  }
  
  // Watch is requesting a list of all routes
  else if(e.payload.request === "ROUTES"){
    var req = new XMLHttpRequest();
    var reqUrl = apiUrl + routesPath;
    console.log("Requesting URL:" + reqUrl);
    req.open("GET", reqUrl, true);
    req.responseType = "json";
    req.setRequestHeader("Cache-Control", "no-cache");
    req.addEventListener("load", function() {
      // Will be called when data is returned from the server
      var resp = this.response;
      var routes = [];
      for (var i = 0; i < resp.length; i++) {
        var route = {"request": "ROUTES"};
        route.route_name = resp[i].Name.trim();
        route.route_group = resp[i].Group.trim();
        route.route_short_name = resp[i].ShortName.trim();
        if(resp[i].Color){
          var route_color = parseCSSColor(resp[i].Color);
          route.route_color_r = route_color[0];
          route.route_color_g = route_color[1];
          route.route_color_b = route_color[2];
        }
        routes.push(route);
      }
      console.log(JSON.stringify(routes));
      console.log(JSON.stringify(resp));
      sendList(routes);
    });
    req.send();
  }
  
  // Watch is requesting a today's pattern for route specified by route_short_name
  else if(e.payload.request === "ROUTE_PATTERN"){
    var req = new XMLHttpRequest();
    var today = new Date();
    var reqUrl = apiUrl + patternPath.format(
      e.payload.route_short_name, 
      today.getFullYear(), 
      today.getMonth()+1, 
      today.getDate()
    );
    console.log("Requesting URL:" + reqUrl);
    req.open("GET", reqUrl, true);
    req.responseType = "json";
    req.setRequestHeader("Cache-Control", "no-cache");
    req.addEventListener("load", function() {
      var resp = this.response;
      var stops = [];
      var minLong = Number.MAX_VALUE;
      var minLat = Number.MAX_VALUE;
      for(var i = 0; i < resp.length; i++) {
        var stop = {"request": "ROUTE_PATTERN"};
        stop.stop_type = StopTypeEnum.STOP_WAYPOINT;
        if(resp[i].PointTypeCode == 1){
          if(resp[i].Stop.IsTimePoint) stop.stop_type = StopTypeEnum.STOP_TIMED;
          else stop.stop_type = StopTypeEnum.STOP_UNTIMED;
          stop.stop_name = resp[i].Name; // A point is only named if the bus actually stops there
        }
        
        stop.stop_long = resp[i].Longtitude;
        stop.stop_lat = resp[i].Latitude;
        minLong = Math.min(minLong, stop.stop_long);
        minLat = Math.min(minLat, stop.stop_lat);
        stops.push(stop);
      }
      for(var i = 0; i < stops.length; i++){
        stops[i].stop_long -= minLong;
        stops[i].stop_lat -= minLat;
      }
      console.log(JSON.stringify(stops));
      console.log(JSON.stringify(resp));
      sendList(stops);
    });
    req.send();
  }
});