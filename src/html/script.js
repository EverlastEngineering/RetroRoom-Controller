R""""(
var eventMethod = window.addEventListener ? "addEventListener" : "attachEvent";
var eventer = window[(eventMethod)];
var messageEvent = eventMethod == "attachEvent" ? "onmessage" : "message";

eventer(messageEvent, function (e) {
	var key = e.message ? "message" : "data";
	xhrget(e[key])
}, false);

function xhrget(data) {
	var xhttp = new XMLHttpRequest();
	xhttp.open("GET", "/" + data + "?cb=" + Math.random(), false);
	xhttp.send();
	console.log(xhttp.responseText)
}
)""""