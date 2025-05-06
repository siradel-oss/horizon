# Changed

* On the web, when the client is served over HTTPS and a resource is requested from an HTTP URL, the request is made using the HTTPS protocol automatically, instead of failing immediately. (The resource may not be available from the HTTPS endpoint, but mixed content is not allowed so HTTP requests cannot succeed.)

# Fixed

* HTTP redirections are now followed on desktop.
