require('bokstd')

histsize=30

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

blob = gfx.texture('blob.png')

t = tracker.new(50, 200)

--metatable for points
pointmeta = {}
pointmeta.__index = pointmeta
function pointmeta:lost(why)
   --print('lost',self,why)
   if self.cluster then
      self.cluster:remove(self)
   end
end

function pointmeta:draw()
   gfx.setstate{colour=self.colour}
   gfx.sprite(self, 5, blob)
--[[
   for i,p in self.history do
      gfx.setstate{colour=gradient(incandescent, 1-(i/histsize))}
      gfx.sprite(p, 5, blob)
   end
]]
end

function pointmeta:move(x, y)
   local dx,dy
   dx = self.x - x
   dy = self.y - y
   self.x,self.y = x,y
   self.dx = dx
   self.dy = dy

   table.insert(self.history, 1, {x=x, y=y, dx=dx, dy=dy})
   table.remove(self.history)
end

function pointmeta:update()
end

function pointmeta:__tostring()
   return string.format('%g,%g', self.x, self.y)
end

function pointmeta.__lt(a,b)
   if a.y == b.y then
      if a.x < b.x then
	 return a
      else
	 return b
      end
   else if a.y < b.y then
	 return a
      else
	 return b
      end
   end
end

--add a new tracked point to the features set
function features:add(idx, x, y, weight)
   pt = { x=x, y=y, key='pt'..unique() }

   local h = {}
   table.setn(h, histsize)
   pt.history = h

   pt.colour = {r=math.random(), g=math.random(), b=math.random()}

   setmetatable(pt, pointmeta)

   self[idx] = pt
   --print('add pt.__mesh=',pt.__mesh)
end

function norm(v)
   return math.sqrt(v.dx*v.dx + v.dy*v.dy)
end

clusters={}

function newcluster(p)
   local c = {}

   function c:add(p)
      assert(p)
      assert(not p.cluster)
   
      self.points[p] = p
      p.cluster = self
   end

   function c:remove(p)
      assert(p.cluster == self)
      self.points[p] = nil
      p.cluster = nil
   end

   function c:draw()
      gfx.setstate{colour=self.colour}

      for p in self.points do
         gfx.sprite(p, 5, blob)
      end
   end


   c.points = {}

   clusters[c] = c

   c:add(p)

   c.colour = {r=math.random(), g=math.random(), b=math.random()}

   return c
end

function closeness(p1, p2)
   local h, ret={}

   for h = 1,histsize do
      local p1h = p1.history[h]
      local p2h = p2.history[h]

      if not p1h or not p2h then
	 return nil
      end

      local a1 = math.atan(p1h.dy/p1h.dx)
      local a2 = math.atan(p2h.dy/p2h.dx)
      
      local c = math.cos(a1-a2)
      local l = 1/((math.abs(norm(p1h)-norm(p2h)))^2 + 1)
      
      local closeness = l*c

      table.insert(ret, closeness)
   end

   return ret
end


function analyze(points)
   gfx.setstate{blend='alpha'}

   for _,p1 in points do
      for _,p2 in points do
	 if p1 ~= p2 and p1.dx and p1.dy and p2.dx and p2.dy then
	    local a1 = math.atan(p1.dy/p1.dx)
	    local a2 = math.atan(p2.dy/p2.dx)
	    
	    local c = math.cos(a1-a2)
	    local l = 1/((math.abs(norm(p1)-norm(p2)))^2 + 1)
	    
	    local closeness = l*c
	    
	    if closeness > .5 then
	       if not p1.cluster and not p2.cluster then
		  local c = newcluster(p1)
		  c:add(p2)
	       else if p1.cluster and not p2.cluster then
		     p1.cluster:add(p2)
		  else if not p1.cluster and p2.cluster then
			p2.cluster:add(p1)
		     else 
			assert(p1.cluster and p2.cluster)
			
			if p1.cluster == p2.cluster then
			   -- still clustered
			else
			   -- interesting
			end
		     end
		  end
	       end
	    end
	 end
      end
   end
end

function process_frame(frame)
   --print('Frame!', frame)

   t:track(features)

   drawframe(frame)


   gfx.setstate{colour={1,1,0,1}, blend='alpha'}

   analyze(features:points())

   if false then
      for _,p in features:points() do
	 p:draw()
      end
   else
      local c
      for c in clusters do
	 c:draw()
      end
   end

   --drawmemuse(frame)   

   --print('gcinfo=', gcinfo())
end
