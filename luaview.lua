-- Example with no feature tracking

require("bokstd")

lua = gfx.texture('lua-logo.png')

history={}
table.setn(history, 4)		--number of frames of history

function process_frame(frame)
   table.insert(history, frame)	--insert at end
   table.remove(history, 1)	--remove from start

   --show old frame as background
   gfx.setstate({colour={}, blend='none'})
   if history[1] then
      local prev = history[1]
      gfx.sprite(prev.width/2, prev.height/2, nil, prev)
   end

   --show current frame masked by Lua logo
   gfx.setstate({blend='alpha'})
   gfx.sprite(frame.width/2, frame.height/2, nil, frame, lua)
end
