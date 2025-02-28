local cjson = require "cjson.safe"

local M = {}

local get = function(object, method, param)

    local params = cjson.decode(param)
    if not params then
        return '{ "code": -1 }'
    end
    return cjson.encode({ code = 0, object = object, method = method, param = params })
end

M.call = function(object, method, param)
    if not method then
        return '{ "code": -1 }'
    end
    if method == "get" then
        return get(object, method, param)
    end

    return '{ "code": 0, "data" : {} }'
end

return M
