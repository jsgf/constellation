require('bokstd')

blob = gfx.texture('blob.png')

m = mesh.new()
t = tracker.new(40,40)

ptmeta = {}
ptmeta.__index = ptmeta
function ptmeta:update()
   -- just reset; the effect will update
   self.tx = self.x
   self.ty = self.y
end

-- call function on each point around pt to implement an area effect
function m:effect(origin, func, range)
   local visited={}

   local function visit(pt)
      local d = dist(pt, origin)

      visited[pt] = pt

      if d < range then
	 func(pt, d)

	 for p in pairs(self:connected(pt)) do
	    if not visited[p] then
	       visit(p)
	    end
	 end
      end
   end

   tri = self:stab(origin)

   if tri then
      visit(tri[1])
      visit(tri[2])
      visit(tri[3])
   end
end

function ptmeta:twitch(scale)
   local dx = (math.random() - .5) * scale
   local dy = (math.random() - .5) * scale

   m:effect(self, function (pt, d) 
		     d = 1/math.exp(d/20)
		     pt.tx = pt.tx + dx*d
		     pt.ty = pt.ty + dy*d
		  end, 50)
end
function ptmeta:init(x,y)
   self.x = x
   self.y = y
   self.tx = x
   self.ty = y
end

width=320
height=240
step=5
across = math.floor(width/step)
down = math.floor(height/step)

do
   local x,y

   for y = 0,height,step do
      for x = 0,width,step do
	 local pt = {}
	 setmetatable(pt, ptmeta)
	 
	 pt:init(x + math.mod(y,step*2)/2,y)
	 m:add(pt)
      end
   end
end

function features:add(idx, x, y, weight)
   pt = { x=x, y=y, age=0, freq=math.random() *.2 + .01 }

   if math.random() > .5 then
      pt.orient = 1
   else
      pt.orient = -1
   end

   function pt:update()
      self.age = self.age+1
   end

   self[idx] = pt
end

function process_frame(frame)
   t:track(features)

   for _,p in pairs(m:points()) do
      p:update()
   end

   for _,p in pairs(features:points()) do
      p:update()

      function magnify(pt, dist)
	 local dx = pt.x - p.x
	 local dy = pt.y - p.y
	 local scale = -.5/math.exp(dist/20)
	 
	 --print('scale=',scale,'d=',d,'dx=',dx*scale,'dy=',dy*scale)
	 pt.tx = pt.tx + dx*scale
	 pt.ty = pt.ty + dy*scale
      end
      
      function twist(pt, dist)
	 -- set origin to p
	 local x = pt.x - p.x
	 local y = pt.y - p.y
	 local scale = .5/math.exp(dist/20) * p.orient
	 scale = scale * (clamp(p.age, 0, 10) / 10)
	 scale = scale * math.sin(p.age * p.freq)
	 local s = math.sin(math.pi * 2 * scale * .2)
	 local c = math.cos(math.pi * 2 * scale * .2)

	 local dx = x - (x * c + y * -s)
	 local dy = y - (x * s + y * c)

	 pt.tx = pt.tx + dx
	 pt.ty = pt.ty + dy
      end

      m:effect(p, magnify, 50)
      --m:effect(p, twist, 50)
   end

   gfx.setstate{color={1,1,1,1}, blend='none'}
   m:draw(frame)

-- [[	Draw features
   gfx.setstate{color={1,1,0,1}, blend='alpha'}
   for _,p in pairs(features:points()) do
      gfx.sprite(p, 5, blob)
   end
--]]

--[[ Draw vectors
   gfx.setstate{color={.5,.5,0,.5}, blend='alpha'}
   for _,p in pairs(m:points()) do
      if p.x ~= p.tx or p.y ~= p.ty then
	 gfx.line(p,{x=p.tx,y=p.ty})
      end
   end
--]]

--[[ Draw mesh
   gfx.setstate{color={.1,.1,.1,1}, blend='add'}
   for e in pairs(m:edges()) do
      gfx.line(e[1],e[2])
   end
--]]
end
