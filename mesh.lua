require('bokstd')

t = tracker.new(30, 50)

m = mesh.new()

blob = gfx.texture('blob.png')

function features.add(self, idx, x, y, weight)
   pt = {x=x, y=y}

   function pt.lost(self, why)
      m:del(self)
      --print('del self.__mesh=', self.__mesh)
   end

   function pt.draw(self)
      gfx.sprite(self.x, self.y, 5, blob)
   end

   function pt.move(self, x, y)
      self.x,self.y = x,y
      m:move(self)
   end

   self[idx] = pt
   m:add(pt)
   --print('add pt.__mesh=',pt.__mesh)
end

function process_frame(frame)
   t:track(features)

   drawframe(frame)

   gfx.setstate{colour={1,1,0,1}, blend='alpha'}
   features:foreach('draw')

   gfx.setstate{colour={}, blend='none'}
   for _,e in {m:edges()} do
      local p1,p2 = e[1], e[2]
      --print('p1=',p1.x,p1.y, 'p2=',p2.x,p2.y)

      if e.age == nil then
	 e.age = 20
      end
      local b = e.age / 20
      gfx.setstate({colour={b,b,b}})
      e.age = e.age - 1
      if e.age < 0 then
	 e.age = 0
      end

      gfx.line(p1, p2)
   end
end

   
