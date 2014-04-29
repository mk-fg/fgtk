--[[
Wrapper
--]]

description = 'Gropes FD activity, see: fg.fdbytes.fin'
short_description = 'I/O bytes, aggregated by field. args: field, interval, top_lines=50'
category = 'fg.io'

args = {
	{argtype='string', name='key', description='the filter field used for groping'},
	{argtype='string', name='interval', description='interval b/w aggregation'},
}

arg_data = {
	key = '',
	interval = 1,
	top_lines = 50,
}


function on_set_arg(name, val)
	if not arg_data[name] then return false end
	if type(arg_data[name]) == 'number' then val = tonumber(val) end
	arg_data[name] = val
	return true
end

function on_init()
	return chisel.exec(
		'fg.fdbytes.fin',
		arg_data.key,
		arg_data.interval,
		arg_data.top_lines )
end
