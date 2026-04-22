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

		// --- Filesystem Tools (Emscripten Module.FS) ---
		var FS_WORK = "";

		function getFS() {
			return (typeof FS !== "undefined") ? FS
				: (typeof Module !== "undefined" && Module.FS) ? Module.FS
				: null;
		}

		function resolveFsPath(relative) {
			var text = String(relative || "");
			var base = text.charAt(0) === "/" ? text : (FS_WORK ? FS_WORK + "/" + text : text);
			var parts = base.split("/");
			var resolved = [];
			for (var i = 0; i < parts.length; i++) {
				if (parts[i] === "" || parts[i] === ".") continue;
				if (parts[i] === "..") {
					if (resolved.length > 0) resolved.pop();
				} else {
					resolved.push(parts[i]);
				}
			}
			return "/" + resolved.join("/");
		}

		function stripFsRoot(fullPath) {
			if (typeof fullPath !== "string" || fullPath.length === 0) return "/";
			return fullPath;
		}

		function syncFs() {
			if (typeof Module !== "undefined" && Module.syncFSRequests !== undefined)
				Module.syncFSRequests++;
		}

		function b64Encode(uint8arr) {
			var bin = "";
			for (var i = 0; i < uint8arr.length; i++)
				bin += String.fromCharCode(uint8arr[i]);
			return btoa(bin);
		}

		function b64Decode(b64) {
			var bin = atob(b64);
			var arr = new Uint8Array(bin.length);
			for (var i = 0; i < bin.length; i++)
				arr[i] = bin.charCodeAt(i);
			return arr;
		}

		function rmTree(FS, path) {
			var entries = FS.readdir(path);
			for (var i = 0; i < entries.length; i++) {
				if (entries[i] === "." || entries[i] === "..") continue;
				var child = path + "/" + entries[i];
				var st = FS.lstat(child);
				if (FS.isDir(st.mode))
					rmTree(FS, child);
				else
					FS.unlink(child);
			}
			FS.rmdir(path);
		}

		function collectPaths(FS, dirPath, result, maxDepth, currentDepth) {
			if (maxDepth > 0 && currentDepth > maxDepth) return;
			var entries;
			try { entries = FS.readdir(dirPath); } catch(e) { return; }
			for (var i = 0; i < entries.length; i++) {
				if (entries[i] === "." || entries[i] === "..") continue;
				var child = dirPath + "/" + entries[i];
				var type = "unknown";
				try {
					var st = FS.lstat(child);
					if (FS.isDir(st.mode)) type = "directory";
					else if (FS.isFile(st.mode)) type = "file";
					else if (FS.isLink(st.mode)) type = "symlink";
				} catch(e) {}
				result.push({ path: stripFsRoot(child), type: type });
				if (type === "directory")
					collectPaths(FS, child, result, maxDepth, currentDepth + 1);
			}
		}

		function statObject(FS, resolved, st) {
			var type = "unknown";
			if (FS.isDir(st.mode)) type = "directory";
			else if (FS.isFile(st.mode)) type = "file";
			else if (FS.isLink(st.mode)) type = "symlink";
			return {
				path: stripFsRoot(resolved),
				type: type,
				size: st.size,
				mode: st.mode,
				mtime: st.mtime ? st.mtime.getTime() : null
			};
		}

		var fsToolDefinitions = [
			{"name":"fs_read_file","description":"Read a file from the TIC-80 virtual filesystem. Returns text by default, or base64-encoded binary when encoding=base64.","inputSchema":{"type":"object","properties":{"path":{"type":"string"},"encoding":{"type":"string","enum":["utf-8","base64"]}},"required":["path"],"additionalProperties":false}},
			{"name":"fs_write_file","description":"Write a file to the TIC-80 virtual filesystem. Content is text by default, or base64-decoded binary when encoding=base64.","inputSchema":{"type":"object","properties":{"path":{"type":"string"},"content":{"type":"string"},"encoding":{"type":"string","enum":["utf-8","base64"]}},"required":["path","content"],"additionalProperties":false}},
			{"name":"fs_append_file","description":"Append content to a file in the TIC-80 virtual filesystem.","inputSchema":{"type":"object","properties":{"path":{"type":"string"},"content":{"type":"string"},"encoding":{"type":"string","enum":["utf-8","base64"]}},"required":["path","content"],"additionalProperties":false}},
			{"name":"fs_exists","description":"Check if a path exists in the TIC-80 virtual filesystem.","inputSchema":{"type":"object","properties":{"path":{"type":"string"}},"required":["path"],"additionalProperties":false}},
			{"name":"fs_stat","description":"Get file/directory metadata (follows symlinks). Returns size, mode, mtime, type.","inputSchema":{"type":"object","properties":{"path":{"type":"string"}},"required":["path"],"additionalProperties":false}},
			{"name":"fs_lstat","description":"Get file/directory metadata without following symlinks.","inputSchema":{"type":"object","properties":{"path":{"type":"string"}},"required":["path"],"additionalProperties":false}},
			{"name":"fs_mkdir","description":"Create a directory. Uses mkdirTree (recursive) by default; set recursive=false for single-level only.","inputSchema":{"type":"object","properties":{"path":{"type":"string"},"recursive":{"type":"boolean"}},"required":["path"],"additionalProperties":false}},
			{"name":"fs_readdir","description":"List entries in a directory. Returns array of entry names.","inputSchema":{"type":"object","properties":{"path":{"type":"string"}},"required":[],"additionalProperties":false}},
			{"name":"fs_readdir_with_filetypes","description":"List entries in a directory with type information (file, directory, symlink).","inputSchema":{"type":"object","properties":{"path":{"type":"string"}},"required":[],"additionalProperties":false}},
			{"name":"fs_rm","description":"Remove a file or directory. Use recursive=true for non-empty directories.","inputSchema":{"type":"object","properties":{"path":{"type":"string"},"recursive":{"type":"boolean"},"force":{"type":"boolean"}},"required":["path"],"additionalProperties":false}},
			{"name":"fs_cp","description":"Copy a file within the TIC-80 virtual filesystem.","inputSchema":{"type":"object","properties":{"source":{"type":"string"},"destination":{"type":"string"}},"required":["source","destination"],"additionalProperties":false}},
			{"name":"fs_mv","description":"Move/rename a file or directory.","inputSchema":{"type":"object","properties":{"source":{"type":"string"},"destination":{"type":"string"}},"required":["source","destination"],"additionalProperties":false}},
			{"name":"fs_chmod","description":"Change file/directory permissions.","inputSchema":{"type":"object","properties":{"path":{"type":"string"},"mode":{"type":"integer"}},"required":["path","mode"],"additionalProperties":false}},
			{"name":"fs_symlink","description":"Create a symbolic link.","inputSchema":{"type":"object","properties":{"target":{"type":"string"},"linkpath":{"type":"string"}},"required":["target","linkpath"],"additionalProperties":false}},
			{"name":"fs_readlink","description":"Read the target of a symbolic link.","inputSchema":{"type":"object","properties":{"path":{"type":"string"}},"required":["path"],"additionalProperties":false}},
			{"name":"fs_realpath","description":"Resolve a path to its canonical absolute form within the TIC-80 virtual filesystem.","inputSchema":{"type":"object","properties":{"path":{"type":"string"}},"required":["path"],"additionalProperties":false}},
			{"name":"fs_resolve_path","description":"Resolve a relative path against the TIC-80 working directory without touching the filesystem. Returns the scoped absolute path.","inputSchema":{"type":"object","properties":{"path":{"type":"string"}},"required":["path"],"additionalProperties":false}},
			{"name":"fs_get_all_paths","description":"Recursively enumerate all file and directory paths under a given root.","inputSchema":{"type":"object","properties":{"path":{"type":"string"},"depth":{"type":"integer"}},"required":[],"additionalProperties":false}}
		];

		function getFsToolDefinitions() { return fsToolDefinitions; }

		function isFsTool(name) {
			return typeof name === "string" && name.indexOf("fs_") === 0;
		}

		function handleFsTool(toolName, args) {
			var FS = getFS();
			if (!FS) return { content: [{type:"text",text:"Filesystem not available (Module.FS not ready)"}], isError: true };

			try {
				switch (toolName) {
				case "fs_read_file": {
					var resolved = resolveFsPath(args.path);
					var encoding = args.encoding || "utf-8";
					var data = FS.readFile(resolved);
					if (encoding === "base64") {
						return { content: [{type:"text",text:b64Encode(data)}], isError: false };
					}
					var txt = "";
					for (var i = 0; i < data.length; i++) txt += String.fromCharCode(data[i]);
					try { txt = decodeURIComponent(escape(txt)); } catch(e) {}
					return { content: [{type:"text",text:txt}], isError: false };
				}
				case "fs_write_file": {
					var resolved = resolveFsPath(args.path);
					var encoding = args.encoding || "utf-8";
					var data = (encoding === "base64") ? b64Decode(args.content) : args.content;
					FS.writeFile(resolved, data);
					syncFs();
					return { content: [{type:"text",text:"OK: wrote " + stripFsRoot(resolved)}], isError: false };
				}
				case "fs_append_file": {
					var resolved = resolveFsPath(args.path);
					var encoding = args.encoding || "utf-8";
					var data = (encoding === "base64") ? b64Decode(args.content) : args.content;
					FS.appendFile(resolved, data);
					syncFs();
					return { content: [{type:"text",text:"OK: appended to " + stripFsRoot(resolved)}], isError: false };
				}
				case "fs_exists": {
					var resolved = resolveFsPath(args.path);
					try { FS.stat(resolved); return { content: [{type:"text",text:"true"}], isError: false }; }
					catch(e) { return { content: [{type:"text",text:"false"}], isError: false }; }
				}
				case "fs_stat": {
					var resolved = resolveFsPath(args.path);
					var st = FS.stat(resolved);
					return { content: [{type:"text",text:JSON.stringify(statObject(FS, resolved, st))}], isError: false };
				}
				case "fs_lstat": {
					var resolved = resolveFsPath(args.path);
					var st = FS.lstat(resolved);
					return { content: [{type:"text",text:JSON.stringify(statObject(FS, resolved, st))}], isError: false };
				}
				case "fs_mkdir": {
					var resolved = resolveFsPath(args.path);
					var recursive = args.recursive !== false;
					if (recursive) FS.mkdirTree(resolved); else FS.mkdir(resolved);
					syncFs();
					return { content: [{type:"text",text:"OK: created directory " + stripFsRoot(resolved)}], isError: false };
				}
				case "fs_readdir": {
					var dirPath = args.path || ".";
					var resolved = resolveFsPath(dirPath);
					var entries = FS.readdir(resolved);
					var filtered = [];
					for (var i = 0; i < entries.length; i++)
						if (entries[i] !== "." && entries[i] !== "..") filtered.push(entries[i]);
					return { content: [{type:"text",text:JSON.stringify(filtered)}], isError: false };
				}
				case "fs_readdir_with_filetypes": {
					var dirPath = args.path || ".";
					var resolved = resolveFsPath(dirPath);
					var entries = FS.readdir(resolved);
					var result = [];
					for (var i = 0; i < entries.length; i++) {
						if (entries[i] === "." || entries[i] === "..") continue;
						var childPath = resolved + "/" + entries[i];
						var type = "unknown";
						try {
							var lst = FS.lstat(childPath);
							if (FS.isDir(lst.mode)) type = "directory";
							else if (FS.isFile(lst.mode)) type = "file";
							else if (FS.isLink(lst.mode)) type = "symlink";
						} catch(e) {}
						result.push({name:entries[i],type:type});
					}
					return { content: [{type:"text",text:JSON.stringify(result)}], isError: false };
				}
				case "fs_rm": {
					var resolved = resolveFsPath(args.path);
					var recursive = !!args.recursive;
					var force = !!args.force;
					try {
						var st = FS.lstat(resolved);
						if (FS.isDir(st.mode)) {
							if (recursive) rmTree(FS, resolved); else FS.rmdir(resolved);
						} else { FS.unlink(resolved); }
						syncFs();
						return { content: [{type:"text",text:"OK: removed " + stripFsRoot(resolved)}], isError: false };
					} catch(e) {
						if (force) return { content: [{type:"text",text:"OK: nothing to remove"}], isError: false };
						return { content: [{type:"text",text:"Error: " + e.message}], isError: true };
					}
				}
				case "fs_cp": {
					var srcResolved = resolveFsPath(args.source);
					var dstResolved = resolveFsPath(args.destination);
					var data = FS.readFile(srcResolved);
					FS.writeFile(dstResolved, data);
					syncFs();
					return { content: [{type:"text",text:"OK: copied " + stripFsRoot(srcResolved) + " to " + stripFsRoot(dstResolved)}], isError: false };
				}
				case "fs_mv": {
					var srcResolved = resolveFsPath(args.source);
					var dstResolved = resolveFsPath(args.destination);
					FS.rename(srcResolved, dstResolved);
					syncFs();
					return { content: [{type:"text",text:"OK: moved " + stripFsRoot(srcResolved) + " to " + stripFsRoot(dstResolved)}], isError: false };
				}
				case "fs_chmod": {
					var resolved = resolveFsPath(args.path);
					FS.chmod(resolved, args.mode);
					syncFs();
					return { content: [{type:"text",text:"OK: chmod " + stripFsRoot(resolved) + " to " + args.mode}], isError: false };
				}
				case "fs_symlink": {
					var target = resolveFsPath(args.target);
					var linkpath = resolveFsPath(args.linkpath);
					FS.symlink(target, linkpath);
					syncFs();
					return { content: [{type:"text",text:"OK: symlink " + stripFsRoot(linkpath) + " -> " + stripFsRoot(target)}], isError: false };
				}
				case "fs_readlink": {
					var resolved = resolveFsPath(args.path);
					var target = FS.readlink(resolved);
					return { content: [{type:"text",text:stripFsRoot(target)}], isError: false };
				}
				case "fs_realpath": {
					var resolved = resolveFsPath(args.path);
					var lookup = FS.lookupPath(resolved);
					return { content: [{type:"text",text:stripFsRoot(lookup.path)}], isError: false };
				}
				case "fs_resolve_path": {
					var resolved = resolveFsPath(args.path);
					return { content: [{type:"text",text:stripFsRoot(resolved)}], isError: false };
				}
				case "fs_get_all_paths": {
					var basePath = args.path || ".";
					var maxDepth = args.depth || 0;
					var resolved = resolveFsPath(basePath);
					var allPaths = [];
					collectPaths(FS, resolved, allPaths, maxDepth, 0);
					return { content: [{type:"text",text:JSON.stringify(allPaths)}], isError: false };
				}
				default:
					return null;
				}
			} catch(e) {
				return { content: [{type:"text",text:"Error: " + (e.message || String(e))}], isError: true };
			}
		}

		function tryInterceptFsMessage(data) {
			if (!isJsonRpcObject(data)) return false;
			if (data.method !== "tools/call") return false;
			var params = data.params;
			if (!params || typeof params !== "object") return false;
			var toolName = params.name;
			if (!isFsTool(toolName)) return false;
			if (data.id === undefined) return false;

			var args = params.arguments || {};
			var result = handleFsTool(toolName, args);
			if (result === null) return false;

			var response = { jsonrpc: "2.0", id: data.id, result: result };

			if (controllerPort)
				controllerPort.postMessage(response);
			else if (controllerWindow)
				controllerWindow.postMessage(response, controllerOrigin);

			return true;
		}

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

			if(tryInterceptFsMessage(event.data))
				return;

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

			if(tryInterceptFsMessage(event.data))
				return;

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

			if(message.result && Array.isArray(message.result.tools))
			{
				var fsDefs = getFsToolDefinitions();
				for(var i = 0; i < fsDefs.length; i++)
					message.result.tools.push(fsDefs[i]);
			}

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
