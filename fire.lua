t = tracker.new(30, 40)

features = {}

function features.foreach(self, func)
   for i in self do
      if type(i) == 'number' then
	 p = features[i]
	 p[func](p)
      end
   end
end

-- particles which need per-frame update
particles = {}

-- particles which need additive blending
render_add = {}

-- particles which need alpha blending
render_alpha = {}

star = gfx.texture('star-la.png')
blob = gfx.texture('blob.png')
smoke = gfx.texture('smoke.png')

breeze=0

-- create a flame particle
function flamept()
end

-- create a smoke particle
function smokept(x, y, intens)
   local pt = { x=x, y=y, intens=intens, age=0 }
   local maxage = 100

   function pt.draw(self)
      local scale = self.age / 10
      local b = (maxage-self.age) / (maxage*20)
      b = b * self.intens

      gfx.setstate({colour={b, b, b, b}})
      gfx.sprite(blob, self.x, self.y, scale)
   end

   function pt.update(self)
      self.age = self.age + 1

      if self.age > maxage or self.intens < .01 then
	 particles[self] = nil
	 render_alpha[self] = nil
      end

      self.y = self.y - 5
      self.x = self.x + breeze
      
      self.intens = self.intens * .9
   end

   --register
   particles[pt] = pt
   render_alpha[pt] = pt
end

-- create floating spark
function sparkpt()
end

-- create a ballistic spark
function spitpt()
end

function lerpcol(p, c1, c2)
   function lerp(p, v1, v2)
      if v1 == nil or v2 == nil then
	 return nil
      else
	 return v1 + (v2-v1) * p
      end
   end
   return { 
      lerp(p, c1[1], c2[1]),
      lerp(p, c1[2], c2[2]),
      lerp(p, c1[3], c2[3]),
      lerp(p, c1[4], c2[4])
   }
end

--color transition; gimp "incandescent" gradient
incandescent =  {
   { .46, { .365, 0, 0 } },
   { .59, { .73,  .01, 0 } },
   { .676, { .863, .269, .097} },
   { .809, { 1,    .545, .196 } },
   { .853, { .986, .743, .135 } },
   { .9, { .973, .938, .081 } },
   { .947, { .974, .953, .450 } },
   { 1, { .976, .968, .822 } },
}

-- return a colour{} for a particular temp
-- temp is arbitrary scale, erm, let's say 0..1
function gradient(cols, temp)
   local pw, pc			--prev values

   -- This could probably be a binary search or something, if it
   -- seemed worthwhile
   for _,col in cols do
      local w,c = unpack(col)	--weight, colour

      if pw then
	 -- if temp is between this entry and the previous, then
	 -- interpolate it
	 if temp > pw and temp <= w then
	    local p = (temp - pw) / (w-pw)
	    return lerpcol(p, pc, c)
	 end
      else
	 -- if temp is before the first entry, then just return the
	 -- first entry
	 if temp <= w then
	    return c
	 end
      end
      pw,pc = w,c
   end

   -- if we ran out of list, just return the last colour
   return pc
end

--[[
A feature being tracked by the tracker

This represents a source of fire, who\'s intensity depends on the
amount of oxygen it gets.  Initially this is set by the the "weight"
(how trackable the feature is), but it dies off pretty quicky as local
oxygen is consumed.  The only way to feed new oxygen is by movement.
]]

average=nil			--average movement
mature=20			--timesteps before full maturity

function trackpoint(x, y, weight)
   local pt = { 
      x=x, y=y,
      px=x, py=y,
      weight=weight, o2=0.01,
      age=0, active=true
   }

   function pt.draw(self)
      local logO2 = math.log(self.o2)
      local scale = logO2 / 50
      local temp = logO2 / 10

      temp = temp < 0 and 0 or temp	-- clamp
      
      if scale < .05 then
	 scale = .05
	 temp = temp + (math.random() * .75)	-- add flicker
      end

      --print('o2=',self.o2,'scale=',scale, 'temp=',temp)
      gfx.setstate({colour=gradient(incandescent, temp)})
      gfx.sprite(star, self.x, self.y, scale)
   end

   function pt.move(self, x, y)
      self.px, self.py = self.x, self.y
      self.x, self.y = x, y

      local dx,dy = (self.x - self.px), (self.y - self.py)
      average.x = average.x + dx
      average.y = average.y + dy
   end

   function pt.update(self)
      self.o2 = self.o2 * .5	-- die off a bit
      
      if self.active then
	 local dx = self.x-self.px
	 local dy = self.y-self.py
	 
	 -- remove overall motion to separate background from
	 -- foreground
	 dx = dx - average.x
	 dy = dy - average.y

	 local delta = dx*dx+dy*dy

	 -- new O2 from motion
	 self.o2 = self.o2 + delta*20

	 -- if immature, add some more from the initial stock
	 if self.age < mature then
	    self.o2 = self.o2 + self.weight/mature
	 end
	 if math.random() > (self.o2 / 100) then
	    smokept(self.x, self.y, self.o2)
	 end

	 self.age = self.age+1
      end

      -- Remove from particle set if we've run out of oxygen (has no
      -- effect its still part of the feature set)
      if self.o2 < .01 then
	 particles[self] = nil
	 render_add[self] = nil
      end
   end

   function pt.lost(self, why)
      if why ~= 'oob' then
	 -- Tracker lost this feature.  Turn it into a particle to
	 -- just fade away
	 self.active = false
	 particles[self] = self
	 render_add[self] = self
      end
   end

   return pt
end

function features.add(self, idx, x, y, weight)
   self[idx] = trackpoint(x, y, weight)
end

function process_frame()
   average={x=0, y=0}
   t:track(features)
   average={x=average.x / t.active, y=average.y / t.active}

   --print(string.format('average=%g, %g', average.x, average.y))

   breeze = breeze + math.random() - .5

   features:foreach('update')
   
   for p in particles do
      p:update()
   end

   gfx.setstate({blend="add"})
   features:foreach('draw')

   for p in render_add do
      p:draw()
   end

   gfx.setstate({blend="alpha"})
   for p in render_alpha do
      p:draw()
   end
end
