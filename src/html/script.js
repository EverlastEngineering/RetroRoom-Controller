R""""(
var eventMethod = window.addEventListener ? "addEventListener" : "attachEvent";
var eventer = window[(eventMethod)];
var messageEvent = eventMethod == "attachEvent" ? "onmessage" : "message";

eventer(messageEvent, function (e) {
	var key = e.message ? "message" : "data";
	// xhrget(e[key])
	socket.send(e[key])
}, false);

function xhrget(data) {
	var xhttp = new XMLHttpRequest();
	xhttp.open("GET", `/${data}?cb=${Math.random()}`, false);
	xhttp.send();
	console.log(xhttp.responseText)
}

let socket = new WebSocket(`ws://${window.location.hostname}/ws`);

socket.onopen = function (e) {
	console.log("[open] Connection established");
	// console.log("Sending to server");
	socket.send("healthcheck");
};

socket.onmessage = function (event) {
	// console.log(`[message] Data received from server: ${event.data}`);
	const message = event.data;
    const iframe = document.getElementById('retroroomiframe');
    iframe.contentWindow.postMessage(message, "*");
};

socket.onclose = function (event) {
	if (event.wasClean) {
		console.log(`[close] Connection closed cleanly, code=${event.code} reason=${event.reason}`);
	} else {
		console.log('[close] Connection died');
		socket = new WebSocket(`ws://${window.location.hostname}/ws`);
	}
};

socket.onerror = function (error) {
	console.log(`[error]`);
};

)""""