--[[
Cloned from: sysdig/chisels/fdbytes_by.lua

Allows to pass interval, top_number.
--]]

description = 'Gropes FD activity, see: fdbytes_by'
short_description = 'I/O bytes, aggregated by field. args: field, interval, top'
category = 'fg.io'

args = {
	{argtype='string', name='key', description='the filter field used for groping'},
	{argtype='string', name='interval', description='interval b/w aggregation'},
	{argtype='string', name='top_lines', description='max number of lines to show (0 = all)'},
}

arg_data = {
	key = '',
	interval = 1,
	top_lines = 0,
}


function on_set_arg(name, val)
	if not arg_data[name] then return false end
	if type(arg_data[name]) == 'number' then val = tonumber(val) end
	arg_data[name] = val
	return true
end

function on_init()
	chisel.exec(
		'fg.tablegen',
		arg_data.key,
		arg_data.key,
		'evt.rawarg.res',
		'Bytes',
		'evt.is_io=true',
		arg_data.top_lines,
		arg_data.interval,
		'bytes' )
	return true
end
