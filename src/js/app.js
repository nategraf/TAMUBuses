var keys = require('message_keys');
var parseCSSColor = require('./csscolorparser').parseCSSColor;

var apiUrl = "http://transport.tamu.edu/BusRoutesFeed/api/";
var routesPath = "Routes";
var myStatus = 1;

var retryWaitOriginal = 50;
var retryWait = retryWaitOriginal;
var pebbleInboxSize = 124; // The defult minimum
var pebbleUsedInbox = 0;

// Used to figure if there is room to send more items
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
  
  else if(e.payload.request == "ROUTES"){
    var req = new XMLHttpRequest();
    var reqUrl = apiUrl + routesPath;
    req.open("GET", reqUrl, true);
    req.responseType = "json";
    req.setRequestHeader("Cache-Control", "no-cache");
    req.addEventListener("load", function() {
      // Will be called when data is returned from the server
      var resp = this.response;
      var routes = [];
      for (var i = 0; i < resp.length; i++) {
        var route = {};
        route.route_name = resp[i].Name.trim();
        route.route_group = resp[i].Group.trim();
        route.route_short_name = resp[i].ShortName.trim();
        route.request = "ROUTES";
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
  
  else if(e.payload.request == "ROUTE_INFO"){
    
  }
});