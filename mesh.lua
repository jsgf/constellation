require('bokstd')

t = tracker.new(5, 10)

m = mesh.new()

blob = gfx.texture('blob.png')

function features.add(self, idx, x, y, weight)
   pt = {x=x, y=y}

   function pt.lost(self, why)
      m.del(self)
   end

   function pt.draw(self)
      gfx.sprite(self.x, self.y, 5, blob)
   end

   self[idx] = pt
   m:add(pt)
end

function process_frame(frame)
   t:track(features)

   drawframe(frame)

   gfx:setstate({colour={1,1,0,1}, blend='alpha'})
   features:foreach('draw')
end

   
