require('bokstd')

t=tracker.new(100,120)

star = gfx.texture('blob.png')

-- Function to construct a new tracked feature point
function trackpoint(x, y, w)
   pt = { x=x, y=y }		-- updated by tracker

   -- drawing function
   function pt.draw(self)
      gfx.sprite(self, 5, star)
   end

   -- We could also define "move" and "lost" functions here, if we
   -- care about those events.

   return pt
end

function features.add(self, idx, x, y, weight)
   self[idx] = trackpoint(x, y, weight)
end

function process_frame(frame)
   --drawframe(frame)

   t:track(features)

   gfx:setstate({colour={1,1,0,1}, blend='alpha'})
   features:foreach('draw')
end
