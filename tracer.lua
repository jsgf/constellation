require('bokstd')

t=tracker.new(20,40)

circle = gfx.texture('circle.png')

-- Function to construct a new tracked feature point
function trackpoint(x, y, w)
   pt = { x=x, y=y }		-- updated by tracker

   -- drawing function
   function pt.draw(self)
      gfx.sprite(self, math.log(w)*2, circle)
   end

   -- We could also define "move" and "lost" functions here, if we
   -- care about those events.

   return pt
end

function features.add(self, idx, x, y, weight)
   self[idx] = trackpoint(x, y, weight)
end

function process_frame(frame)
   gfx:setstate{colour={1,1,1,1}}
   drawframe(frame)

   t:track(features)

   gfx:setstate({colour={1,1,0,1}, blend='add'})
   features:foreach('draw')
end
