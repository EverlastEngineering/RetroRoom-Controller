R""""(
var eventMethod = window.addEventListener ? "addEventListener" : "attachEvent";
var eventer = window[(eventMethod)];
var messageEvent = eventMethod == "attachEvent" ? "onmessage" : "message";
let socket;

eventer(messageEvent, function (e) {
	var key = e.message ? "message" : "data";
	// xhrget(e[key])
	if (e[key] == 'ws_init') ws_init();
	else if (socket.readyState == WebSocket.OPEN) socket.send(e[key])
	else console.debug("Unable to send message to socket connection.")
}, false);

function xhrget(data) {
	var xhttp = new XMLHttpRequest();
	xhttp.open("GET", `/${data}?cb=${Math.random()}`, false);
	xhttp.send();
	console.debug(xhttp.responseText)
}

function ws_init() {
	if (socket && (socket.readyState == WebSocket.OPEN || socket.readyState == WebSocket.CONNECTING)) return;
	console.debug(`Attempting new websockets connection.`)
	socket = new WebSocket(`ws://${window.location.hostname}/ws`);
	socket.onopen = function (e) {
		console.debug("[open] Connection established");
	};
	socket.onmessage = function (event) {
		console.debug(`[message] Data received from server: ${event.data}`);
		const message = event.data;
		const iframe = document.getElementById('retroroomiframe');
		iframe.contentWindow.postMessage(message, "*");
	};
	socket.onclose = function (event) {
		if (event.wasClean) {
			console.debug(`[close] Connection closed cleanly, code=${event.code} reason=${event.reason}`);
		} else {
			console.debug('[close] Connection died');
			ws_init();
		}
	};
	socket.onerror = function (error) {
		console.debug(`[error] Oh noez. Anyway.`);
	};
}
ws_init();

)""""