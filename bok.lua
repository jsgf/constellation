require('bokstd')

fmt = string.format

t = tracker.new(40, 80)

features = {}			-- features tracked by tracker

particles = {}			-- particles

--tex = gfx.texture('foosldjf')
star = gfx.texture('star.png')
print(fmt("star: fmt=%s %dx%d", star.format, star.width, star.height))

butterfly = gfx.texture('atlantis.png')

-- a simple particle under the influence of gravity
function particle(x, y, dx, dy, life)
   pt = { x=x, y=y, dx=dx, dy=dy, life=life, lifetime=life }

   function pt.update(self)
      self.x = self.x + self.dx
      self.y = self.y + self.dy
      self.dy = self.dy + .3

      self.life = self.life - 1
      if self.life <= 0 then
	 particles[self] = nil
      end
   end

   function pt.draw(self)
      local b = self.life / self.lifetime * 2
      local sb = math.sqrt(b)

      gfx.setstate({ colour={sb, b, b, b}, blend="add" })
      gfx.sprite(self, 10, star)
   end

   return pt
end

mature=10

function lostpoint(lostpt)
   pt = { 
      x=lostpt.x, y=lostpt.y, 
      dx=lostpt.x-lostpt.px, 
      dy=lostpt.y-lostpt.py, 
      age=0, size=20 }

   function pt.update (self)
      self.x = self.x + self.dx
      self.y = self.y + self.dy

      self.size = self.size * 1.2
      self.dx = self.dx * 1.2
      self.dy = self.dy * 1.2

      self.age = self.age+1
      if self.age > 10 then
	 particles[self] = nil
      end
   end       
   
   function pt.draw (self) 
      local b = 1 - (self.age/10)
      --print("age=",self.age,"b=",b)
      gfx.setstate({colour={b,b,b,b}, blend="alpha"})
      gfx.sprite(self, self.size, butterfly)
   end

   particles[pt] = pt
end

-- create a new feature being tracked
function trackpoint(x, y, weight)
   pt = { x=x, y=y, px=x, py=y, weight=weight, age=0 }

   function pt.draw(self)
      if self.age <= mature then
	 local b = self.age/mature
	 gfx.setstate({colour={b,b,b,b}, blend="alpha"})
      else
	 gfx.setstate({colour={1,1,1,1}, blend="alpha"})
      end

      gfx.sprite(self, 20, butterfly)
   end

   function pt.move(self, x, y)
      self.px = self.x
      self.py = self.y
      self.x = x
      self.y = y

      if self.age > mature and math.random() < .2 then
	 p = particle(x, y, math.random() * 4 - 2, -math.random()*3, 20)
	 particles[p] = p
      end

      self.age = self.age + 1
   end

   function pt.lost(self, why)
      if self.age >= mature and why ~= "oob" then
	 lostpoint(self)
      end
   end

   return pt
end

-- tracker found a new feature
function features.add(self, idx, x, y, val)
   -- print(fmt("new point %d (%f, %f, %d)", idx, x, y, val))

   self[idx] = trackpoint(x, y, val)
end

function process_frame(frame)
   drawframe(frame)

   t:track(features)
   if false then
      print("\n----------------------------------------");
      print(fmt("tracker has %d features", 
		t.active))
   end

   for p in pairs(particles) do
      p:update()
   end

   for p in pairs(particles) do
      p:draw()
   end

   gfx.setstate({blend="alpha", colour={1,1,0,1}})
   for i in pairs(features) do
      if type(i) == 'number' then
	 features[i]:draw()
      end
   end
   gfx.setstate({blend="none"})
end
