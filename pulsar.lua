-- Very simple pulsing thing

require("bokstd")

t=tracker.new(30, 50)		-- 30-50 features tracked

star = gfx.texture('star.png')

-- Function to construct a new tracked feature point
function trackpoint(x, y, w)
   pt = { x=x, y=y }		-- updated by tracker

   local twopi = math.pi*2

   -- internal state
   local logw = math.log(w)
   local step = (twopi / 100) * (logw + 1)
   local phase = 0

   -- drawing function
   function pt.draw(self)
      local size = (math.sin(phase) + 1) * logw*2 + 10
      phase = phase + step

      gfx.sprite(star, self.x, self.y, size)
   end

   -- We could also define "move" and "lost" functions here, if we
   -- care about those events.

   return pt
end

function features.add(self, idx, x, y, weight)
   self[idx] = trackpoint(x, y, weight)
end


function process_frame(frame)
   drawframe(frame)

   t:track(features)

   gfx.setstate({blend='add'})
   features:foreach('draw')
end
