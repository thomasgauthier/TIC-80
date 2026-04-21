Module.saveAs = Module.saveAs||function(e){"use strict";if(typeof e==="undefined"||typeof navigator!=="undefined"&&/MSIE [1-9]\./.test(navigator.userAgent)){return}var t=e.document,n=function(){return e.URL||e.webkitURL||e},r=t.createElementNS("http://www.w3.org/1999/xhtml","a"),o="download"in r,i=function(e){var t=new MouseEvent("click");e.dispatchEvent(t)},a=/constructor/i.test(e.HTMLElement),f=/CriOS\/[\d]+/.test(navigator.userAgent),u=function(t){(e.setImmediate||e.setTimeout)(function(){throw t},0)},d="application/octet-stream",s=1e3*40,c=function(e){var t=function(){if(typeof e==="string"){n().revokeObjectURL(e)}else{e.remove()}};setTimeout(t,s)},l=function(e,t,n){t=[].concat(t);var r=t.length;while(r--){var o=e["on"+t[r]];if(typeof o==="function"){try{o.call(e,n||e)}catch(i){u(i)}}}},p=function(e){if(/^\s*(?:text\/\S*|application\/xml|\S*\/\S*\+xml)\s*;.*charset\s*=\s*utf-8/i.test(e.type)){return new Blob([String.fromCharCode(65279),e],{type:e.type})}return e},v=function(t,u,s){if(!s){t=p(t)}var v=this,w=t.type,m=w===d,y,h=function(){l(v,"writestart progress write writeend".split(" "))},S=function(){if((f||m&&a)&&e.FileReader){var r=new FileReader;r.onloadend=function(){var t=f?r.result:r.result.replace(/^data:[^;]*;/,"data:attachment/file;");var n=e.open(t,"_blank");if(!n)e.location.href=t;t=undefined;v.readyState=v.DONE;h()};r.readAsDataURL(t);v.readyState=v.INIT;return}if(!y){y=n().createObjectURL(t)}if(m){e.location.href=y}else{var o=e.open(y,"_blank");if(!o){e.location.href=y}}v.readyState=v.DONE;h();c(y)};v.readyState=v.INIT;if(o){y=n().createObjectURL(t);setTimeout(function(){r.href=y;r.download=u;i(r);h();c(y);v.readyState=v.DONE});return}S()},w=v.prototype,m=function(e,t,n){return new v(e,t||e.name||"download",n)};if(typeof navigator!=="undefined"&&navigator.msSaveOrOpenBlob){return function(e,t,n){t=t||e.name||"download";if(!n){e=p(e)}return navigator.msSaveOrOpenBlob(e,t)}}w.abort=function(){};w.readyState=w.INIT=0;w.WRITING=1;w.DONE=2;w.error=w.onwritestart=w.onprogress=w.onwrite=w.onabort=w.onerror=w.onwriteend=null;return m}(typeof self!=="undefined"&&self||typeof window!=="undefined"&&window||this.content);if(typeof module!=="undefined"&&module.exports){module.exports.saveAs=saveAs}else if(typeof define!=="undefined"&&define!==null&&define.amd!==null){define([],function(){return saveAs})};
Module.showAddPopup = function(callback)
{
	var modal = document.getElementById('add-modal');
	var span = document.getElementsByClassName("close")[0];
	modal.style.display = "block";
	function cancel(){modal.style.display = "none"; callback(null, null);}
	span.onclick = cancel;
	window.onclick = function(event) {if (event.target == modal) cancel();}

	var uploadInput = document.getElementById('upload-input');
	uploadInput.onchange = function()
	{
		var file = uploadInput.files[0];

		if(!file) return;

		var reader = new FileReader();

		reader.onload = function(event)
		{
			var rom = new Uint8Array(event.target.result);

			callback(file.name, rom);

			uploadInput.value = "";
			modal.style.display = "none";
		};

		reader.readAsArrayBuffer(file);
	};
};

(function() {
	var root = typeof window !== "undefined" ? window : null;
	var moduleObject = typeof Module !== "undefined" ? Module : null;

	function createBridge() {
		var queue = [];
		var controllerWindow = null;
		var controllerOrigin = "*";
		var controllerPort = null;
		var MAX_REQUEST_BYTES = 8192;
		var POPUP_PORT_PROTOCOL = "tic80ctl-popup-port-v1";

		function readPopupHandshakeConfig() {
			if(!root || !root.location || typeof URLSearchParams === "undefined")
				return { token: "", origin: "" };

			try
			{
				var params = new URLSearchParams(root.location.search || "");
				return {
					token: params.get("tic80ctl_popup_token") || "",
					origin: params.get("tic80ctl_popup_origin") || "",
				};
			}
			catch(error)
			{
				return { token: "", origin: "" };
			}
		}

		var popupHandshake = readPopupHandshakeConfig();

		function requestPopupPort() {
			if(popupHandshake.token && root && root.opener) {
				root.opener.postMessage({
					tic80ctlBridge: POPUP_PORT_PROTOCOL,
					type: "request_port",
					token: popupHandshake.token
				}, popupHandshake.origin || "*");
			}
		}

		function hasControllerWindow() {
			clearBindingIfClosed();
			return !!controllerPort || !!controllerWindow;
		}

		function isJsonRpcObject(value) {
			return !!value
				&& typeof value === "object"
				&& !Array.isArray(value)
				&& value.jsonrpc === "2.0";
		}

		function isBridgeControlMessage(value) {
			return !!value
				&& typeof value === "object"
				&& !Array.isArray(value)
				&& value.tic80ctlBridge === POPUP_PORT_PROTOCOL;
		}

		function getMethodName(value) {
			if(!isJsonRpcObject(value) || typeof value.method !== "string")
				return "";
			return value.method;
		}

		function clearControllerPort() {
			if(controllerPort)
			{
				controllerPort.onmessage = null;
				try
				{
					controllerPort.close();
				}
				catch(error)
				{
				}
			}

			controllerPort = null;
		}

		function clearControllerWindow() {
			controllerWindow = null;
			controllerOrigin = "*";
		}

		function canBindWindowController(event) {
			if(!event || !event.source || event.source === root)
				return false;
			if(controllerPort)
				return controllerWindow === event.source;
			if(!controllerWindow || controllerWindow === event.source)
				return true;
			if(controllerWindow.closed)
				return true;
			return getMethodName(event.data) === "initialize";
		}

		function bindWindowController(event) {
			if(!canBindWindowController(event))
				return false;

			controllerWindow = event.source;
			controllerOrigin = event.origin || "*";
			return true;
		}

		function clearBindingIfClosed() {
			if(controllerWindow && controllerWindow.closed && !controllerPort)
			{
				clearControllerWindow();
			}
		}

		function buildJsonRpcErrorMessage(id, code, message) {
			return {
				jsonrpc: "2.0",
				id: id === undefined ? null : id,
				error: {
					code: code,
					message: message,
				},
			};
		}

		function sendJsonRpcErrorToWindow(targetWindow, targetOrigin, id, code, message) {
			if(!targetWindow || !message) return;

			targetWindow.postMessage(buildJsonRpcErrorMessage(id, code, message), targetOrigin || "*");
		}

		function sendJsonRpcErrorToPort(port, id, code, message) {
			if(!port || !message) return;

			port.postMessage(buildJsonRpcErrorMessage(id, code, message));
		}

		function enqueueSerializedRequest(serialized, rejectOversized) {
			if(typeof serialized !== "string") return false;

			if(lengthBytesUTF8(serialized) + 1 > MAX_REQUEST_BYTES)
			{
				if(rejectOversized)
					rejectOversized();
				return false;
			}

			queue.push(serialized);
			return true;
		}

		function attachControllerPort(port, sourceWindow, origin) {
			clearControllerPort();
			controllerPort = port;
			controllerWindow = sourceWindow || controllerWindow;
			controllerOrigin = origin || controllerOrigin || "*";

			if(controllerPort.start)
				controllerPort.start();

			controllerPort.onmessage = enqueueFromPort;
		}

		function tryBindPopupPort(event) {
			var data = event && event.data;

			if(!root || !root.opener || !popupHandshake.token)
				return false;
			if(event.source !== root.opener)
				return false;
			if(!isBridgeControlMessage(data) || data.type !== "connect")
				return false;
			if(data.token !== popupHandshake.token)
				return false;
			if(popupHandshake.origin && event.origin !== popupHandshake.origin)
				return false;

			var port = event.ports && event.ports[0];
			if(!port)
				return true;

			attachControllerPort(port, event.source, event.origin || "*");
			controllerPort.postMessage({
				tic80ctlBridge: POPUP_PORT_PROTOCOL,
				type: "ready",
				token: popupHandshake.token,
			});

			return true;
		}

		function enqueueFromController(event) {
			if(tryBindPopupPort(event))
				return;
			if(controllerPort)
				return;
			if(!isJsonRpcObject(event.data)) return;
			clearBindingIfClosed();
			if(!bindWindowController(event)) return;

			var serialized;

			try
			{
				serialized = JSON.stringify(event.data);
			}
			catch(error)
			{
				return;
			}

			enqueueSerializedRequest(serialized, function() {
				sendJsonRpcErrorToWindow(controllerWindow, controllerOrigin, event.data.id, -32600, "Request too large");
			});
		}

		function enqueueFromPort(event) {
			if(!controllerPort || !isJsonRpcObject(event.data)) return;

			var serialized;

			try
			{
				serialized = JSON.stringify(event.data);
			}
			catch(error)
			{
				return;
			}

			enqueueSerializedRequest(serialized, function() {
				sendJsonRpcErrorToPort(controllerPort, event.data.id, -32600, "Request too large");
			});
		}

		function attachListener() {
			if(!root || !root.addEventListener) return;
			root.addEventListener("message", enqueueFromController);
			requestPopupPort();
		}

		function copyToHeap(serialized, buffer, bufferSize) {
			var required = lengthBytesUTF8(serialized) + 1;

			if(!buffer || bufferSize <= 0) return required;
			if(required > bufferSize) return required;

			stringToUTF8(serialized, buffer, bufferSize);
			return required;
		}

		function peekRequiredBytes() {
			if(!queue.length) return 0;
			return lengthBytesUTF8(queue[0]) + 1;
		}

		function popIntoBuffer(buffer, bufferSize) {
			if(!queue.length) return 0;

			var serialized = queue[0];
			var required = copyToHeap(serialized, buffer, bufferSize);

			if(required > bufferSize) return required;

			queue.shift();
			return required;
		}

		function sendSerializedResponse(serialized) {
			var message;

			clearBindingIfClosed();
			if(!hasControllerWindow()) return 0;
			if(typeof serialized !== "string" || !serialized.length) return 0;

			try
			{
				message = JSON.parse(serialized);
			}
			catch(error)
			{
				return 0;
			}

			if(!isJsonRpcObject(message)) return 0;

			if(controllerPort)
				controllerPort.postMessage(message);
			else
				controllerWindow.postMessage(message, controllerOrigin);

			return 1;
		}

		return {
			attachListener: attachListener,
			hasControllerWindow: hasControllerWindow,
			getBindingState: function() {
				clearBindingIfClosed();
				return {
					bound: !!controllerPort || !!controllerWindow,
					origin: controllerOrigin,
					transport: controllerPort ? "port" : controllerWindow ? "window" : "none",
				};
			},
			clearBinding: function() {
				clearControllerPort();
				clearControllerWindow();
			},
			hasPendingRequests: function() {
				return queue.length > 0;
			},
			getQueueLength: function() {
				return queue.length;
			},
			peekNextRequestSize: peekRequiredBytes,
			popNextRequestIntoBuffer: popIntoBuffer,
			popNextSerializedRequest: function() {
				return queue.length ? queue.shift() : null;
			},
			sendSerializedResponse: sendSerializedResponse,
		};
	}

	var bridge = createBridge();
	bridge.attachListener();

	if(moduleObject)
	{
		moduleObject.tic80McpBridge = bridge;
	}
})();
