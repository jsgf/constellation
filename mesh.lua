require('bokstd')

t = tracker.new(5, 10)

m = mesh.new()

blob = gfx.texture('blob.png')

function features.add(self, idx, x, y, weight)
   pt = {x=x, y=y}

   function pt.lost(self, why)
      m:del(self)
      print('del self.__mesh=', self.__mesh)
   end

   function pt.draw(self)
      gfx.sprite(self.x, self.y, 5, blob)
   end

   self[idx] = pt
   m:add(pt)
   print('add pt.__mesh=',pt.__mesh)
end

function process_frame(frame)
   t:track(features)

   --drawframe(frame)

   gfx:setstate({colour={1,1,0,1}, blend='alpha'})
   features:foreach('draw')

   gfx:setstate({colour={}, blend='none'})
   for _,e in {m:edges()} do
      --print('p=',p.x,p.y)
      print(e)
      print(unpack(e[1]), unpack(e[2]))
      gfx:line(e[1].x,e[1].y, e[2].x,e[2].y)
   end
   --gfx.point(unpack(m:points()))
end

   
