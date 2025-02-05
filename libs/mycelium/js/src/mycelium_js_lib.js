var LibraryMycelium = {
    $Mycelium: {
        queries: [],
        GL_TIME_ELAPSED_EXT: 0x88bf,
        GL_GPU_DISJOINT_EXT: 0x8fbb,
        COMPLETION_STATUS_KHR: 0x91b1,
    },

    my_js_begin_query_elapsed_time__sig: "i",
    my_js_begin_query_elapsed_time: function () {
        var gl = GL.currentContext.GLctx;
        var query = gl.createQuery();
        var id = GL.getNewId(Mycelium.queries);
        query.name = id;
        Mycelium.queries[id] = query;
        gl.beginQuery(Mycelium.GL_TIME_ELAPSED_EXT, query);

        return id;
    },

    my_js_end_time_query__sig: "v",
    my_js_end_time_query: function () {
        var gl = GL.currentContext.GLctx;
        gl.endQuery(Mycelium.GL_TIME_ELAPSED_EXT);
    },

    my_js_query_time_elapsed__sig: "di",
    my_js_query_time_elapsed: function (id) {
        var gl = GL.currentContext.GLctx;
        var query = Mycelium.queries[id];

        if (!query) {
            console.warn("Time query", id, "does not exist!");
        }

        var available = gl.getQueryParameter(query, gl.QUERY_RESULT_AVAILABLE);
        var disjoint = gl.getParameter(Mycelium.GL_GPU_DISJOINT_EXT);
        if (available && !disjoint) {
            var elapsed_time = gl.getQueryParameter(query, gl.QUERY_RESULT);
            return elapsed_time;
        }

        if (disjoint) {
            // clear all queries.
            Mycelium.queries = [];
            return -2;
        }

        return -1;
    },

    my_js_delete_query__sig: "vi",
    my_js_delete_query: function (id) {
        var gl = GL.currentContext.GLctx;

        var query = Mycelium.queries[id];
        gl.deleteQuery(query);
        Mycelium.queries[id] = null;
    },

    my_js_check_program_link_completion_status__sig: "ii",
    my_js_check_program_link_completion_status: function (id) {
        var gl = GL.currentContext.GLctx;
        return gl.getProgramParameter(GL.programs[id], Mycelium.COMPLETION_STATUS_KHR);
    },
};

autoAddDeps(LibraryMycelium, "$GL");
autoAddDeps(LibraryMycelium, "$Mycelium");

mergeInto(LibraryManager.library, LibraryMycelium);
