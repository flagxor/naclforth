var forth;
var accept_buffer = '';
var reading_line = false;
var event_buffer = '';
var last_output = '';

function startEvents() {
  document.onkeypress = function(evt) {
    var ch = evt.which;
    if (ch == undefined) ch = evt.keyCode;
    event_buffer += String.fromCharCode(ch);
    processKeys();
    return false;
  };
  document.onkeydown = function(evt) {
    event_buffer += String.fromCharCode(evt.keyCode + 256);
    processKeys();
    if (evt.keyCode == 8) return false;
    return true;
  };
  document.onkeyup = function(evt) {
    event_buffer += String.fromCharCode(evt.keyCode + 512);
    processKeys();
    if (evt.keyCode == 8) return false;
    return true;
  };
  document.onpaste = function(evt) {
    var data = evt.clipboardData.getData('text/plain');
    data = data.replace(/\u2003/g, ' ');
    data = data.replace(/\r/g, '');
    data = data.replace(/\n/g, '\r');
    event_buffer += data;
    processKeys();
    return true;
  };
}

function htmlEscape(str) {
  return str.replace(
      /&/g, '&amp;').replace(
        />/g, '&gt;').replace(
          /</g, '&lt;').replace(
            /"/g, '&quot;');
}

function processKeys() {
  if (!reading_line) return;
  for (var i = 0; i < event_buffer.length; i++) {
    var ch = event_buffer.substr(i, 1);
    var code = event_buffer.charCodeAt(i);
    if (code == 8 || code == 8 + 256) {
      if (accept_buffer.length) {
        accept_buffer = accept_buffer.substr(
            0, accept_buffer.length - 1);
      }
    } else if (code == 13) {
      reading_line = false;
      var line = accept_buffer;
      accept_buffer = '';
      appendOut(htmlEscape(line) + '\n');
      event_buffer = event_buffer.substr(i + 1);
      refreshPrompt();
      forth.postMessage(line);
      return;
    } else if (code >= 32 && code <= 126) {
      accept_buffer += ch;
    }
  }
  event_buffer = '';
  refreshPrompt();
}

function appendOut(text) {
  var span = document.createElement('span');
  span.innerHTML = text;
  var out = document.getElementById('out');
  out.appendChild(span); 
}

function refreshPrompt() {
  var prompt = document.getElementById('prompt');
  prompt.innerHTML = htmlEscape(accept_buffer);
  window.scrollTo(0, document.body.scrollHeight);
}

function httpRequest(url, method, values, callback) {
  var request = new XMLHttpRequest();
  request.overrideMimeType('text/plain; charset=x-user-defined');
  request.onreadystatechange = function() {
    if (request.readyState == 1) {
      request.withCredentials = true;
    }
    if (request.readyState == 4) {
      callback(request.status, request.responseText);
    }
  };
  request.open(method, url);
  if (method == 'POST') {
    //request.setRequestHeader('Content-Type',
    //    'application/x-www-form-urlencoded');
    //request.setRequestHeader('Content-Type', 'multipart/form-data');
    var fd = new FormData();
    for (key in values) {
      fd.append(key, values[key]);
    }
    request.send(fd);
  } else {
    request.send(null);
  }
};

function handleMessage(msg) {
  var prefix = msg.data.substr(0, 1);
  var rest = msg.data.substr(1);
  switch (prefix) {
    // sync
    case 's': forth.postMessage(''); break;
              // alert
    case 'a': alert(rest); break;
              // javascript
    case 'j': eval(rest); break;
              // setcursor
    case 'c':
              document.getElementById('cursor').innerHTML = rest;
              break;
              // page
    case 'p': document.getElementById('out').innerHTML = rest; break;
              // type
    case 'o': appendOut(htmlEscape(rest)); break;
              // rawtype
    case 'O': appendOut(rest); break;
              // getevent
    case 'i':
              var tmp = event_buffer.substr(0, 1);
              event_buffer = event_buffer.substr(1);
              forth.postMessage(tmp);
              break;
              // getevents
    case 'I':
              var tmp = event_buffer;
              event_buffer = '';
              forth.postMessage(tmp);
              break;
              // readline
    case 'r': reading_line = true; processKeys(); break;
              // http
    case 'h':
              var parts = rest.split('|');
              var method = parts[0];
              var url = parts[1];
              parts = parts.slice(2);
              var values = {};
              while (parts.length) {
                if (parts[0].substr(0, 1) == '~') {
                  values[parts[0].substr(1)] = parts.slice(1).join('|');
                  parts = [];
                } else {
                  values[parts[0]] = parts[1];
                  parts = parts.slice(2);
                }
              }
              httpRequest(url, method, values, function(status, text) {
                forth.postMessage(
                  String.fromCharCode(Math.floor(status / 100) % 100) +
                  String.fromCharCode(status % 100) + text);
              });
              break;
    default:
              appendOut('ERROR - Unknown message prefix "' +
                  prefix + '"\n');
              break;
  }
}

function start(text) {
  forth = document.getElementById('forth');
  forth.addEventListener('message', handleMessage);
  forth.postMessage(text);
}

function bootForAccount(boot) {
  var status = 'http://naclforth.appspot.com/rawstatus';
  httpRequest(status, 'GET', {}, function(status, text) {
    if (status == 200) {
      start(text + ' ' + boot);
    } else {
      start('-1 ' + boot);
    }
  });
}

function moduleDidLoad() {
  var boot = 'http://naclforth.appspot.com/_read?owner=0&filename=%2fpublic%2f_boot';
  httpRequest(boot, 'GET', {}, function(status, text) {
    if (status == 200) {
      bootForAccount(text);
    } else {
      appendOut('ERROR - Unable to load boot code, ');
      appendOut('falling back to built-ins!\n');
      bootForAccount('');
    }
  });
}

function init() {
  startEvents();
  document.getElementById('listener').addEventListener(
      'load', moduleDidLoad, true);
}

init();
