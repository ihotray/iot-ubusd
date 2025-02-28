--[[
@file iot-ubusd.lua
@brief iot-ubusd的Lua回调处理脚本

该脚本负责处理标准ubus调用的回调逻辑:
1. 解析JSON参数
2. 根据method调用不同的处理函数
3. 返回JSON格式的处理结果
]]

local cjson = require "cjson.safe"

local M = {}

-- @brief 处理get方法调用
-- @param object 对象名称
-- @param method 方法名称
-- @param param JSON格式参数字符串
-- @return JSON格式的响应字符串
local get = function(object, method, param)

    local params = cjson.decode(param)
    if not params then
        return '{ "code": -1 }'
    end
    return cjson.encode({ code = 0, object = object, method = method, param = params })
end

-- @brief 处理所有ubus调用的入口函数
-- @param object 对象名称
-- @param method 方法名称
-- @param param JSON格式参数字符串
-- @return JSON格式的响应字符串
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
