# iot-ubusd

iot-ubusd 是一个基于ubus的RPC服务程序，它提供了一个灵活的对象和方法注册机制，支持通过JSON配置文件动态添加ubus对象。

## 重要
- **禁止在ubus方法的响应实现代码中递归调用ubus接口
- 支持直接调用iot-rpcd实现的接口
```shell
root@openwrt:~# ubus -t 1 call iot-ubusd iot-rpc '{ "method" : "is_inited", "param" : { "username": "admin"} }'
{
        "data": {
                "inited": true
        },
        "method": "is_inited",
        "code": 0
}
```
- 也可以通过json配置文件和lua代码动态扩展新对象和方法
```json
[
    {
        "object": "iot-ubusd-sample",
        "method": [
            {
                "name": "set",
                "param": [
                    {
                        "name": "method",
                        "type": "BLOBMSG_TYPE_STRING"
                    },
                    {
                        "name": "param",
                        "type": "BLOBMSG_TYPE_TABLE"
                    }
                ]
            },
            {
                "name": "get",
                "param": [
                    {
                        "name": "data",
                        "type": "BLOBMSG_TYPE_UNSPEC"
                    }
                ]
            }
        ]
    }
]
```
```lua
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

```
```shell
root@openwrt:~# ubus call iot-ubusd-sample get '{"test": "test"}'
{
        "param": {
                "test": "test"
        },
        "method": "get",
        "object": "iot-ubusd-sample",
        "code": 0
}
```

## 功能特性

- 支持通过JSON配置文件动态注册ubus对象和方法
- 支持Lua脚本处理RPC调用
- 支持调试级别配置
- 支持标准的ubus消息格式

## 编译

```bash
make
```

## 命令行参数

```
Usage: iot-ubusd OPTIONS
  -c PATH  - ubusd对象配置文件路径, 默认: '/www/iot/etc/iot-ubusd.json'
  -v LEVEL - 调试级别, 0-4, 默认: 1
```

## 配置文件格式

配置文件采用JSON格式，例如:

```json
[
    {
        "object": "object-name",
        "method": [
            {
                "name": "method-name",
                "param": [
                    {
                        "name": "param-name",
                        "type": "BLOBMSG_TYPE_STRING"
                    }
                ]
            }
        ]
    }
]
```

支持的参数类型:
- BLOBMSG_TYPE_STRING
- BLOBMSG_TYPE_INT32
- BLOBMSG_TYPE_BOOL 
- BLOBMSG_TYPE_TABLE
- BLOBMSG_TYPE_ARRAY
- BLOBMSG_TYPE_UNSPEC

## Lua回调处理

程序支持两种Lua脚本处理:

1. 标准ubus调用处理: `/usr/share/iot/rpc/ubus/iot-ubusd.lua`
2. IoT RPC处理: `/www/iot/iot-rpc.lua`

Lua脚本需要实现call()函数来处理RPC请求，函数签名:

```lua
call(object, method, param)
```

参数:
- object: 对象名称
- method: 方法名称 
- param: JSON格式的参数字符串

返回值需要是JSON格式字符串。

## 架构设计

程序主要包含以下模块:

1. 主程序初始化和参数解析(main.c)
2. ubus对象和方法管理(ubusd.c)
3. Lua脚本回调处理(iot-ubusd.lua)

工作流程:

1. 读取命令行参数
2. 初始化ubus连接
3. 解析JSON配置文件
4. 注册ubus对象和方法
5. 启动事件循环
6. 接收ubus请求
7. 调用Lua脚本处理请求
8. 返回处理结果

## 错误处理

程序使用JSON格式返回错误信息，格式如下:

```json
{
    "code": -1,
    "msg": "错误描述"
}
```

常见错误码:
- 0: 成功
- -1: 一般错误

## 依赖

- libubox
- libubus 
- lua
- libiot
