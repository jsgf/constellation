require('bokstd')

blob = gfx.texture('blob.png')

--t = tracker.new(50, 200)
t = tracker.new(20, 20)

--metatable for points
meta_point = {}
meta_point.__index = meta_point
function meta_point:lost(why)
   --print('lost',self,why)

   triangles:del(self)
end

function meta_point:draw()
   gfx.setstate{colour=self.colour}
   gfx.sprite(self, 5, blob)
end

function meta_point:move(x, y)
   local dx,dy
   dx = self.x - x
   dy = self.y - y
   self.x,self.y = x,y
   self.dx = dx
   self.dy = dy
end

function meta_point:update()
end

function meta_point:__tostring()
   return string.format('%g,%g', self.x, self.y)
end

function meta_point.__lt(a,b)
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

   pt.colour = { r=math.random(), g=math.random(), b=math.random() }

   pt.triangles = {}

   setmetatable(pt, meta_point)

   self[idx] = pt

   -- construct all the triangles with this point
   for _,p1 in self:points() do
      for _,p2 in self:points() do
	 if pt ~= p1 and pt ~= p2 and p1 ~= p2 then
	    triangles:new(p1, p2, pt)
	 end
      end
   end
end

-- --

-- Set of all existing triangles.  This table has several keys:
--  - points -> triangles
--  - triangle set
triangles={}

function angle(p1, p2)
   local dx,dy = p1.x-p2.x, p1.y-p2.y

   return math.atan2(dy, dx)
end

meta_triangle = {}
meta_triangle.__index = meta_triangle

-- Return a scalar representing how much the angles have changed since 
-- the triangle was created
function meta_triangle:diff()
   local angles = {
      angle(self[1], self[2]), 
      angle(self[2], self[3]), 
      angle(self[3], self[1]) }

   local c1, c2, c3
   c1 = math.cos(2*(angles[1] - self.angles[1]))
   c2 = math.cos(2*(angles[2] - self.angles[2]))
   c3 = math.cos(2*(angles[3] - self.angles[3]))

   return c1*c2*c3
end

function meta_triangle:__tostring()
   return string.format('(%s; %s; %s)', 
			tostring(self[1]), 
			tostring(self[2]), 
			tostring(self[3]))
end

function meta_triangle:draw()
   local a = self:diff()

   if a < .9 then
      gfx.setstate{color={1,0,0}}
   else
      gfx.setstate{color={1,1,0}}
   end
   gfx.line(self[1], self[2], self[3])
end

function meta_triangle:key()
   return string.format('%s:%s:%s', self[1].key, self[2].key, self[3].key)
end

function triangles:new(p1, p2, p3)
   local tri = { p1, p2, p3 }

   setmetatable(tri, meta_triangle)

   -- sort points into canonical form
   table.sort(tri, function(p1, p2) return p1.key < p2.key end)

   tri.angles = { 
      angle(tri[1], tri[2]),
      angle(tri[2], tri[3]), 
      angle(tri[3], tri[1]) }

   self[tri] = tri

   for _,p in {p1,p2,p3} do
      p[tri] = tri
   end

   p1.triangles[tri] = tri
   p2.triangles[tri] = tri
   p3.triangles[tri] = tri

   --print(tri:key())
end

function triangles:del(p)
   for t in p.triangles do
      --print('for pt', p, 'matched', t)
      assert(t[1] == p or t[2] == p or t[3] == p)
      self[t] = nil
   end
   p.triangles={}
end

-- --
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

   gfx.setstate{colour={1,1,1,1}, blend='none'}
   t:track(features)

   drawframe(frame)

   gfx.setstate{colour={1,1,0,1}, blend='alpha'}

   --analyze(features:points())

   if true then
      for _,p in features:points() do
	 p:draw()
      end

      gfx.setstate{colour={1,1,0,1}, blend='none'}
      for a,b in triangles do
	 if a==b then
	    a:draw()
	 end
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
