"use strict";

var fs = require('fs');
var path = require('path');
var http = require('http');

var staticBasePath = './';

var cache = {};

var staticServe = function(req, res) {
    var resolvedBase = path.resolve(staticBasePath);
    var safeSuffix = path.normalize(req.url).replace(/^(\.\.[\/\\])+/, '');
    var fileLoc = path.join(resolvedBase, safeSuffix);
    
        var stream = fs.createReadStream(fileLoc);
        console.log(fileLoc, fileLoc.substring(-3))

        const safeResource = () => res.setHeader('Cross-Origin-Resource-Policy', 'same-origin');
        if (fileLoc.endsWith('worker.js')) {
            res.setHeader('Content-Type', "application/javascript");
            res.setHeader('Cross-Origin-Embedder-Policy', 'require-corp');
            // safeResource();
        }
        else if (fileLoc.endsWith('.js')) {
            res.setHeader('Content-Type', "application/javascript");
            safeResource();
        } else if (fileLoc.endsWith('.wasm')) {
            res.setHeader('Content-Type', 'application/wasm');
            safeResource();
        } else if (fileLoc.endsWith('.css')) {
            res.setHeader('Content-Type', 'text/css');
            safeResource();
        } else {
            res.setHeader('Content-Type', "text/html");
            res.setHeader('Cross-Origin-Opener-Policy', 'same-origin');
            res.setHeader('Cross-Origin-Embedder-Policy', 'require-corp');
        }
        // Handle non-existent file
        stream.on('error', function(error) {
            res.writeHead(404, 'Not Found');
            res.write('404: File Not Found!');
            res.end();
        });

        // File exists, stream it to user
        res.statusCode = 200;
        stream.pipe(res);
};

var httpServer = http.createServer(staticServe);

httpServer.listen(8000);