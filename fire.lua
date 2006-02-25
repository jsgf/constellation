require ("bokstd")

maxpoints=50
t = tracker.new(maxpoints/2, maxpoints)

-- particles which need per-frame update
particles = {}

-- particles which need additive blending
render_add = {}

-- particles which need alpha blending
render_alpha = {}

function add_particle(p, render)
   particles[p] = p
   if render then
      render[p] = p
   end
end

function del_particle(p)
   particles[p] = nil
   render_add[p] = nil
   render_alpha[p] = nil
end

star = gfx.texture('star-la.png')
blob = gfx.texture('blob.png')
smoke = gfx.texture('smoke.png')

breeze=0

-- create a flame particle
function flamept()
end


smokepop={}
table.setn(smokepop, 500)	--max number of smoke particles

-- create a smoke particle
function smokept(x, y, intens)
   local pt = { x=x, y=y, intens=intens, age=1 }
   local maxage = 100

   function pt.draw(self)
      local minsize = 20
      local size = self.age * 3
      local b = (maxage-self.age) / (maxage*20)
      b = b * self.intens
      if size < minsize then
	 b = b * (size/minsize)
	 scale = minsize
      end

      gfx.setstate({colour={b, b, b, b}})
      gfx.sprite(self, size, blob)
   end

   function pt.update(self)
      self.age = self.age + 1

      if self.age > maxage or
	 self.intens < .2 or
	 self.y < 0 then
	 del_particle(self)
      end

      self.y = self.y - 5
      self.x = self.x + breeze
      
      self.intens = self.intens * .9
   end

   --register
   add_particle(pt, render_alpha)

   if smokepop[1] then
      del_particle(smokepop)
      table.remove(smokepop, 1)
   end
   table.insert(smokepop, pr)
end

-- create floating spark
function sparkpt()
end

-- create a ballistic spark
function spitpt()
end

-- color transition; gimp "incandescent" gradient
incandescent =  {
   { .46,  { .365,  0,    0   } },
   { .59,  { .73,  .01,   0   } },
   { .676, { .863, .269, .097 } },
   { .809, { 1,    .545, .196 } },
   { .853, { .986, .743, .135 } },
   { .9,   { .973, .938, .081 } },
   { .947, { .974, .953, .450 } },
   { 1,    { .976, .968, .822 } },
}

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

   -- Drawing size and colour are functions of the particles oxygen.
   function pt.draw(self)
      local logO2 = math.log(self.o2)
      local scale = logO2 * 2
      local temp = logO2 / 10

      temp = clamp(temp, 0, temp)
      
      -- if the scale is too small, then clamp, and a
      -- flickering ember effect
      if scale < 5 then
	 scale = 5
	 temp = temp + (math.random() * .5)	-- add flicker
      end

      --print('o2=',self.o2,'scale=',scale, 'temp=',temp)

      -- set the colour depending on the temperature
      gfx.setstate({colour=gradient(incandescent, temp)})
      gfx.sprite(self, scale, blob)
   end

   -- Movement is simple; it just keeps track of the previous
   -- position, and adds the delta into the average
   function pt.move(self, x, y)
      self.px, self.py = self.x, self.y
      self.x, self.y = x, y

      local dx,dy = (self.x - self.px), (self.y - self.py)
      average.x = average.x + dx
      average.y = average.y + dy
   end

   --[[ 
   The point may be active or lost; active is when it is being
   driven by the tracker.  Lost means that it is clearing up.
   Each timestep, the o2 level is reduced by 25%.  If active, then
   the distance^2 moved since the last frame is used to add to the
   particle\'s O2 level.  If the particle is still immature, then
   O2 is accumulated depending on its initial weight.  The amount
   of smoke depends on the o2 level too. ]]
   function pt.update(self)
      self.o2 = self.o2 * .75	-- die off a bit
      
      if self.active then
	 local dx = self.x-self.px
	 local dy = self.y-self.py
	 
	 if false then
	    -- remove overall motion to separate background from
	    -- foreground
	    dx = dx - average.x
	    dy = dy - average.y
	 end

	 local delta = dx*dx+dy*dy

	 -- new O2 from motion
	 self.o2 = self.o2 + delta*20

	 -- if immature, add some more from the initial stock
	 if self.age < mature then
	    self.o2 = self.o2 + self.weight/mature
	 end

	 -- This is interestingly wrong.  The original intent was that
	 -- the smoke level be proportional to the o2 level.  This code
	 -- gets it wrong, but the effect is to only generate smoke when
	 -- the point is guttering, which looks better.
	 if math.random() > (self.o2 / 70) then
	    smokept(self.x, self.y, self.o2)
	 end

	 self.age = self.age+1
      end

      -- Remove from particle set if we've run out of oxygen (has no
      -- effect its still part of the feature set)
      if self.o2 < .01 then
	 del_particle(self)
      end
   end

   function pt.lost(self, why)
      if why ~= 'oob' then
	 -- Tracker lost this feature.  Turn it into a particle to
	 -- just fade away
	 if false then
	    self.active = false
	    add_particle(self, render_add)
	 end
      end
   end

   return pt
end

function features.add(self, idx, x, y, weight)
   self[idx] = trackpoint(x, y, weight)
end

function process_frame(frame)
   gfx.setstate{colour={1,1,1,1}}
   drawframe(frame)

   average={x=0, y=0}
   t:track(features)
   average={x=average.x / t.active, y=average.y / t.active}

   --print(string.format('average=%g, %g', average.x, average.y))
   breeze = clamp(breeze + math.random() - .5, -5, 5)

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
