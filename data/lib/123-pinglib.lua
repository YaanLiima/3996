---Pinglib by mock the bear
ping = {
    _VERSION = "1.0";
    _AUTHOR = "Mock the bear";
    test = function()
        if not getPlayerLastPong then
            print('Error! Cannot run this lib without source changes.')
            return false
        else
            return true
        end
    end,
    CONST_WATING_RESPONSE = -3,
    CONST_DISCONECTED = -2,
}
 
function ping.CheckPing(cid) -- getPlayerPing By mock
    local c = getPlayerLastPong(cid)
    local l = getPlayerLastPing(cid)
    if not c or not l then
        return -2
    end
    local ping = math.floor((c-l)/10)
    if ping < 0 then
        if ping*-1 > 2000 then
            return -2
        end
        return -3
    end
    return ping
end
 
function ping.loop(cid,storage,f,...) -- check
    if not isPlayer(cid) then
        return false
    end
    local p_ing = ping.CheckPing(cid)
    if p_ing ~= CONST_WATING_RESPONSE then
        if not tonumber(p_ing) then
            doPlayerSetStorageValue(cid,storage,ping.CONST_DISCONECTED)
            return
        else
            doPlayerSetStorageValue(cid,storage,p_ing)
                        f(cid,storage,p_ing,...)
            return
        end
    end
    addEvent(ping.loop,100,cid,storage,f,...)
end
 
function ping.getPing(cid,storage,f,...) --- This function will send a ping request and wait the response, so then will add an value on a storage.
    if ping.test() then
        doPlayerSetStorageValue(cid,storage,ping.CONST_WATING_RESPONSE)
        doPlayerSendPing(cid)
        ping.loop(cid,storage,f,...)
    end
end

function getPlayerPing(cid) -- getPlayerPing By mock
    local c = getPlayerLastPong(cid)
    local l = getPlayerLastPing(cid)
    if not c or not l then
        return 'Disconected'
    end
    local ping = math.floor((c-l)/10)
    if ping < 0 then
        if ping*-1 > 2000 then
            return 'Disconected'
        end
        return 'Wating response'
    end
    return ping
end