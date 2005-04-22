require('bokstd')

blob = gfx.texture('blob.png')

m = mesh.new()

ptmeta = {}
ptmeta.__index = ptmeta
function ptmeta:update()
   local dx=self.tx - self.x
   local dy=self.ty - self.y

   self.tx = self.tx - dx*.1
   self.ty = self.ty - dy*.1

   if math.random() < .001 then
      self:twitch(50)
   end
end

-- call function on each point around self to implement an area effect
function ptmeta:effect(func, range)
   local visited={}

   local function visit(pt)
      local d = dist(pt, self)

      visited[pt] = pt

      if d < range then
	 func(pt, d)

	 for p in m:connected(pt) do
	    if not visited[p] then
	       visit(p)
	    end
	 end
      end
   end

   visit(self)
end
function ptmeta:twitch(scale)
   local dx = (math.random() - .5) * scale
   local dy = (math.random() - .5) * scale

   self:effect(function (pt, d) 
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

   table.insert(points, self)
end

width=320
height=240
step=10
across = math.floor(width/step)
down = math.floor(height/step)

points={}
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

function process_frame(frame)
   if first then
      first = false
      local x,y,step
   end

   for _,p in m:points() do
      p:update()
   end
   
   gfx.setstate{color={1,1,1,1}, blend='none'}
   m:draw(frame)
   
   s={x=math.random(frame.width), y=math.random(frame.height)}
   
   t=m:stab(s)
   gfx.setstate{color={1,1,0}, blend='alpha'}
   gfx.sprite(s, 5, blob);
   gfx.setstate{color={0,1,0}}
   gfx.sprite(t[1], 5, blob);
   gfx.sprite(t[2], 5, blob);
   gfx.sprite(t[3], 5, blob);

-- [[
   gfx.setstate{color={.1,.1,.1,1}, blend='add'}
   for e in m:edges() do
      gfx.line(e[1],e[2])
   end
--]]
end
