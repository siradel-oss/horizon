// SPDX-FileCopyrightText: Copyright 2020 Siradel
// SPDX-License-Identifier: MIT

var LibraryHorizon = {
    $Horizon: {
        fetchRequests: {},
        nextFetchRequestHandle: 1,

        getNextFetchRequestHandle: function () {
            var handle = Horizon.nextFetchRequestHandle;
            Horizon.nextFetchRequestHandle++;
            return handle;
        },

        cleanFetchRequest: function (handle) {
            var request = Horizon.fetchRequests[handle];

            if (request) {
                if (request.headers !== null) {
                    _free(request.headers);
                }

                delete Horizon.fetchRequests[handle];
            }
        },

        canvas: null,
        resizeObserver: null,
    },

    hrz_js_get_user_agent__proxy: "sync",
    hrz_js_get_user_agent__sig: "p",
    hrz_js_get_user_agent: function () {
        var ua = navigator.userAgent;
        var byteLength = lengthBytesUTF8(ua) + 1;
        var buffer = _malloc(byteLength);
        stringToUTF8(ua, buffer, byteLength);
        return buffer;
    },

    hrz_js_get_base_url__proxy: "sync",
    hrz_js_get_base_url__sig: "p",
    hrz_js_get_base_url: function () {
        var url = document.baseURI;
        var byteLength = lengthBytesUTF8(url) + 1;
        var buffer = _malloc(byteLength);
        stringToUTF8(url, buffer, byteLength);
        return buffer;
    },

    // From https://github.com/emscripten-core/emscripten/blob/fc67fbf94b6b815c619e75dfe59c84f3c81bf698/src/library_browser.js#L981
    //
    // `hrz_js_fetch_*` functions are not proxied to the main thread. Consequently, because JS objects,
    // including `Horizon.fetchRequests`, are not shared between workers, a request started from one
    // worker must be handled entirely from that worker. (Another worker would not know the request.)
    hrz_js_fetch_data__sig: "ipddppppppi",
    hrz_js_fetch_data: function (
        urlRawStr,
        rangeStart,
        rangeSize,
        headersJsonRawStr,
        requestRawStr,
        paramRawStr,
        onload,
        onerror,
        ptr_arg,
        int_arg
    ) {
        var url = UTF8ToString(urlRawStr);
        var headersJson = UTF8ToString(headersJsonRawStr);
        var request = UTF8ToString(requestRawStr);
        var param = UTF8ToString(paramRawStr);

        if (location.protocol === "https:" && url.substr(0, 5).toLowerCase() === "http:") {
            // We are trying to fetch a resource over HTTP from an HTTPS page. This is mixed content and
            // is not allowed by the browser and it would be blocked, but we can upgrade the request and
            // try to fetch the resource over HTTPS instead.
            // However fetching from localhost using HTTP when the page is secure is allowed, so we leave
            // these URLs unchanged. The regex matches `*.localhost`, `127.x.x.x`, and `[::1]`.
            var parsedUrl = URL.parse(url);
            if (
                parsedUrl &&
                !parsedUrl.hostname.match(
                    /^(?:(?:.+\.)?localhost)|(?:127(?:\.(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)){3})|(?:\[::1\])$/
                )
            ) {
                parsedUrl.protocol = "https";
                url = parsedUrl.href;
            }
        }

        var xhr = new XMLHttpRequest();
        var handle = Horizon.getNextFetchRequestHandle();

        Horizon.fetchRequests[handle] = {
            xhr: xhr,
            url: url,
            headers: null,
            additionalHeaders: {},
            complete: false,
            byteArray: null,
        };

        xhr.open(request, url, true);
        xhr.responseType = "arraybuffer";

        // LOAD
        xhr.onload = function xhr_onload(e) {
            var statusCode = xhr.status;
            if (
                (statusCode >= 200 && statusCode < 300) ||
                (statusCode === 0 && url.substr(0, 4).toLowerCase() != "http")
            ) {
                var request = Horizon.fetchRequests[handle];
                if (request) {
                    request.byteArray = new Uint8Array(/** @type{ArrayBuffer} */ (xhr.response));
                    request.complete = true;

                    if (rangeStart > 0 || rangeSize > 0) {
                        if (statusCode !== 206) {
                            // We only wanted a range of the data, but the server responded
                            // with the full data (or the data was encoded in the URL).
                            // We have to extract the desired range ourselves.
                            const fullSize = request.byteArray.byteLength;

                            request.byteArray =
                                rangeSize > 0
                                    ? request.byteArray.subarray(rangeStart, rangeStart + rangeSize)
                                    : request.byteArray.subarray(rangeStart);

                            statusCode = 206;
                            Horizon.fetchRequests[handle].additionalHeaders["Content-Range"] =
                                "bytes " +
                                rangeStart +
                                "-" +
                                (rangeStart + request.byteArray.length - 1) +
                                "/" +
                                fullSize;
                        } else {
                            if (request.byteArray.byteLength !== rangeSize) {
                                console.error(
                                    "Partial request response has invalid size: expected " +
                                        rangeSize +
                                        ", got " +
                                        request.byteArray.byteLength
                                );
                            }
                        }
                    }
                }

                if (onload) {
                    // prettier-ignore
                    { {{{ makeDynCall('viiii', 'onload') }}}(handle, statusCode, ptr_arg, int_arg); }
                }
            } else {
                if (onerror) {
                    // prettier-ignore
                    { {{{ makeDynCall('viiiii', 'onerror') }}}(handle, statusCode, xhr.statusText, ptr_arg, int_arg); }
                }
            }
        };

        // ERROR
        xhr.onerror = function xhr_onerror(e) {
            var request = Horizon.fetchRequests[handle];
            if (request && onerror) {
                // prettier-ignore
                { {{{ makeDynCall('viiiii', 'onerror') }}}(handle, xhr.status, xhr.statusText, ptr_arg, int_arg); }
            }
            Horizon.cleanFetchRequest(handle);
        };

        if (rangeStart > 0 || rangeSize > 0) {
            var rangeStr = "bytes=" + rangeStart + "-";
            if (rangeSize > 0) {
                rangeStr += rangeStart + rangeSize - 1;
            }
            xhr.setRequestHeader("Range", rangeStr);
        }

        if (headersJson.length > 2) {
            var headers = JSON.parse(headersJson);
            if (Array.isArray(headers)) {
                for (var i = 0; i < headers.length; i += 2) {
                    xhr.setRequestHeader(headers[i], headers[i + 1]);
                }
            }
        }

        if (request == "POST") {
            // Send the proper header information along with the request
            xhr.setRequestHeader("Content-type", "application/x-www-form-urlencoded");
            xhr.send(param);
        } else {
            xhr.send(null);
        }

        return handle;
    },

    hrz_js_fetch_get_headers__sig: "pi",
    hrz_js_fetch_get_headers: function (handle) {
        var request = Horizon.fetchRequests[handle];
        if (!request || !request.xhr) {
            console.error("Unknown fetch request");
            return 0;
        }

        if (request.headers !== null) {
            return request.headers;
        }

        var headersMap = {};
        Object.entries(request.additionalHeaders).forEach(([k, v]) => {
            headersMap[k] = v;
        });

        // Snippet mostly from https://developer.mozilla.org/en-US/docs/Web/API/XMLHttpRequest/getAllResponseHeaders
        var allHeaders = request.xhr
            .getAllResponseHeaders()
            .trim()
            .split(/[\r\n]+/);

        allHeaders.forEach((line) => {
            const parts = line.split(":");
            const name = parts.shift().trim();
            const value = parts.join(":").trim();
            headersMap[name] = value;
        });

        var headersArray = [];
        Object.entries(headersMap).forEach(([k, v]) => {
            headersArray.push(k, v);
        });

        if (allHeaders.length > 0) {
            var serializedHeaders = JSON.stringify(headersArray);
            var headersLength = lengthBytesUTF8(serializedHeaders) + 1;
            request.headers = _malloc(headersLength);
            stringToUTF8(serializedHeaders, request.headers, headersLength);
        } else {
            request.headers = 0;
        }

        return request.headers;
    },

    hrz_js_fetch_get_data_size__sig: "ii",
    hrz_js_fetch_get_data_size: function (handle) {
        var request = Horizon.fetchRequests[handle];
        if (!request) {
            console.error("Unknown fetch request");
            return 0;
        }
        if (!request.complete || !request.byteArray) {
            console.error("Request is not complete");
            return 0;
        }

        return request.byteArray.byteLength;
    },

    hrz_js_fetch_get_data__sig: "pi",
    hrz_js_fetch_get_data: function (handle) {
        var request = Horizon.fetchRequests[handle];
        if (!request) {
            console.error("Unknown fetch request");
            return 0;
        }
        if (!request.complete || !request.byteArray) {
            console.error("Request is not complete");
            return 0;
        }

        var buffer = _malloc(request.byteArray.length);
        HEAPU8.set(request.byteArray, buffer);
        return buffer;
    },

    hrz_js_fetch_copy_data__sig: "iipi",
    hrz_js_fetch_copy_data: function (handle, data, dataSize) {
        var request = Horizon.fetchRequests[handle];
        if (!request) {
            console.error("Unknown fetch request");
            return false;
        }
        if (!request.complete || !request.byteArray) {
            console.error("Request is not complete");
            return false;
        }
        if (dataSize != request.byteArray.length) {
            console.error(
                "Destination buffer has wrong size: " +
                    dataSize +
                    ", expecting " +
                    request.byteArray.length
            );
            return false;
        }

        HEAPU8.set(request.byteArray, data);
        return true;
    },

    hrz_js_fetch_abort__sig: "vi",
    hrz_js_fetch_abort: function (handle) {
        var request = Horizon.fetchRequests[handle];
        if (request && request.xhr) {
            request.xhr.abort();
        }
    },

    hrz_js_fetch_clean__sig: "vi",
    hrz_js_fetch_clean: function (handle) {
        Horizon.cleanFetchRequest(handle);
    },

    $set_pointer_event_handler: function (eventName, targetIdRawStr, userData, capture, callback) {
        var targetId = UTF8ToString(targetIdRawStr);
        var targetElement = document.querySelector(targetId);

        if (callback !== 0) {
            targetElement[eventName] = function (event) {
                // pointerType values match the PointerType enum values in 'hrz_core_platform_emscripten.cpp'.
                var pointerType = 0;
                if (event.pointerType === "touch") pointerType = 1;
                else if (event.pointerType === "pen") pointerType = 2;
                else if (event.pointerType === "mouse") pointerType = 3;

                var modKeys = 0;
                if (event.ctrlKey) modKeys |= 1;
                if (event.shiftKey) modKeys |= 2;
                if (event.altKey) modKeys |= 4;

                if (pointerType != 0) {
                    // prettier-ignore
                    { {{{ makeDynCall('viiffii', 'callback') }}}(event.pointerId, pointerType, event.offsetX, event.offsetY, event.buttons, modKeys, userData); }

                    if (capture) {
                        targetElement.setPointerCapture(event.pointerId);
                    }
                }
            };
        } else {
            targetElement[eventName] = null;
        }
    },

    hrz_js_set_pointer_down_handler__proxy: "sync",
    hrz_js_set_pointer_down_handler__sig: "vppip",
    hrz_js_set_pointer_down_handler__deps: ["$set_pointer_event_handler"],
    hrz_js_set_pointer_down_handler: function (targetIdRawStr, userData, capture, callback) {
        set_pointer_event_handler("onpointerdown", targetIdRawStr, userData, capture, callback);
    },

    hrz_js_set_pointer_up_handler__proxy: "sync",
    hrz_js_set_pointer_up_handler__sig: "vppip",
    hrz_js_set_pointer_up_handler__deps: ["$set_pointer_event_handler"],
    hrz_js_set_pointer_up_handler: function (targetIdRawStr, userData, capture, callback) {
        set_pointer_event_handler("onpointerup", targetIdRawStr, userData, capture, callback);
    },

    hrz_js_set_pointer_move_handler__proxy: "sync",
    hrz_js_set_pointer_move_handler__sig: "vppip",
    hrz_js_set_pointer_move_handler__deps: ["$set_pointer_event_handler"],
    hrz_js_set_pointer_move_handler: function (targetIdRawStr, userData, capture, callback) {
        set_pointer_event_handler("onpointermove", targetIdRawStr, userData, capture, callback);
    },

    hrz_js_set_pointer_cancel_handler__proxy: "sync",
    hrz_js_set_pointer_cancel_handler__sig: "vppip",
    hrz_js_set_pointer_cancel_handler__deps: ["$set_pointer_event_handler"],
    hrz_js_set_pointer_cancel_handler: function (targetIdRawStr, userData, capture, callback) {
        set_pointer_event_handler("onpointercancel", targetIdRawStr, userData, capture, callback);
    },

    hrz_js_set_pointer_leave_handler__proxy: "sync",
    hrz_js_set_pointer_leave_handler__sig: "vppip",
    hrz_js_set_pointer_leave_handler__deps: ["$set_pointer_event_handler"],
    hrz_js_set_pointer_leave_handler: function (targetIdRawStr, userData, capture, callback) {
        set_pointer_event_handler("onpointerleave", targetIdRawStr, userData, capture, callback);
    },

    // From https://stackoverflow.com/a/30810322
    hrz_js_copy_string_to_clipboard__proxy: "sync",
    hrz_js_copy_string_to_clipboard__sig: "vp",
    hrz_js_copy_string_to_clipboard: function (rawStr) {
        var str = UTF8ToString(rawStr);

        if (navigator.clipboard) {
            navigator.clipboard.writeText(str).then(
                function () {
                    console.log("Text copied to clipboard");
                },
                function (err) {
                    console.error("Could not copy text to clipboard: ", err);
                }
            );
        } else {
            var textArea = document.createElement("textarea");
            textArea.value = str;

            // Avoid scrolling to bottom
            textArea.style.top = "0";
            textArea.style.left = "0";
            textArea.style.position = "fixed";

            document.body.appendChild(textArea);
            textArea.focus();
            textArea.select();

            try {
                if (document.execCommand("copy")) {
                    console.log("Text copied to clipboard");
                } else {
                    console.error("Could not copy text to clipboard");
                }
            } catch (err) {
                console.error("Could not copy text to clipboard:", err);
            }

            document.body.removeChild(textArea);
        }
    },

    hrz_js_register_canvas__proxy: "sync",
    hrz_js_register_canvas__sig: "ip",
    hrz_js_register_canvas: function (canvasSelectorRawStr) {
        var canvasSelector = UTF8ToString(canvasSelectorRawStr);
        Horizon.canvas = document.querySelector(canvasSelector);
        return Horizon.canvas != null;
    },

    hrz_js_install_resize_observer__proxy: "sync",
    hrz_js_install_resize_observer__sig: "ipp",
    hrz_js_install_resize_observer: function (callbackPtr, userData) {
        if (Horizon.resizeObserver != null) {
            console.error("Cannot install resize observer more than once");
            return false;
        }

        if (Horizon.canvas == null) {
            console.error("Cannot install resize observer before the canvas has been registered");
            return false;
        }

        Horizon.resizeObserver = new ResizeObserver((entries) => {
            if (entries.length > 0) {
                let entry = entries[0];

                let cssWidth, cssHeight;
                let deviceWidth, deviceHeight;

                // Adapted from https://webglfundamentals.org/webgl/lessons/webgl-resizing-the-canvas.html
                if (entry.devicePixelContentBoxSize) {
                    deviceWidth = entry.devicePixelContentBoxSize[0].inlineSize;
                    deviceHeight = entry.devicePixelContentBoxSize[0].blockSize;
                    cssWidth = deviceWidth / window.devicePixelRatio;
                    cssHeight = deviceHeight / window.devicePixelRatio;
                } else {
                    if (entry.contentBoxSize) {
                        if (entry.contentBoxSize[0]) {
                            cssWidth = entry.contentBoxSize[0].inlineSize;
                            cssHeight = entry.contentBoxSize[0].blockSize;
                        } else {
                            cssWidth = entry.contentBoxSize.inlineSize;
                            cssHeight = entry.contentBoxSize.blockSize;
                        }
                    } else {
                        cssWidth = entry.contentRect.width;
                        cssHeight = entry.contentRect.height;
                    }
                    deviceWidth = Math.round(cssWidth * window.devicePixelRatio);
                    deviceHeight = Math.round(cssHeight * window.devicePixelRatio);
                }
                // prettier-ignore
                { {{{ makeDynCall("vffffp", "callbackPtr") }}}(cssWidth, cssHeight, deviceWidth, deviceHeight, userData); }
            }
        });

        Horizon.resizeObserver.observe(Horizon.canvas);

        console.log("Resize observer installed");
        return true;
    },

    hrz_js_uninstall_resize_observer__proxy: "sync",
    hrz_js_uninstall_resize_observer__sig: "v",
    hrz_js_uninstall_resize_observer: function () {
        if (Horizon.resizeObserver != null) {
            Horizon.resizeObserver.disconnect();
            Horizon.resizeObserver = null;
            console.log("Resize observer uninstalled");
        }
    },

    hrz_js_get_device_pixel_ratio__proxy: "sync",
    hrz_js_get_device_pixel_ratio__sig: "f",
    hrz_js_get_device_pixel_ratio: function () {
        return window.devicePixelRatio;
    },
};

autoAddDeps(LibraryHorizon, "$Horizon");

mergeInto(LibraryManager.library, LibraryHorizon);
