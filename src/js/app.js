var keys = require('message_keys');

var apiUrl = "http://transport.tamu.edu/BusRoutesFeed/api/";
var routesPath = "Routes";
var myStatus = 1;

var retryWaitOriginal = 50;
var retryWait = retryWaitOriginal;
var pebbleInboxSize = 124; // The defult minimum

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
        routes.push({
          "route_name": resp[i].Name.trim(),
          "route_group": resp[i].Group.trim(),
          "route_short_name": resp[i].ShortName.trim(),
          "request": "ROUTES"
        });
      }
      console.dir(routes);
      sendList(routes);
    });
    req.send();
  }
  
  else if(e.payload.request == "ROUTE_INFO"){
    
  }
});