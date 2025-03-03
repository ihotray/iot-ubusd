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

M.call = function(args)
    return cjson.encode({ code = 0, param = args })
end

return M
