t = tracker.new(40, 80)

features = {}

particles = {}

blob = gfx.texture('star-la.png')

function trackpoint(x, y)
   local pt = {x=x, y=y}

   function pt.draw(self)
      gfx.sprite(blob, self.x, self.y, .1)
   end

   return pt
end

function features.add(self, idx, x, y)
   self[idx] = trackpoint(x, y)
end

function process_frame()
   t:track(features)

   for p in particles do
      p:update()
   end

   for p in particles do
      p:draw()
   end

   gfx.setstate({blend="1-alpha", colour={1,1,0,1}})
   for i in features do
      if type(i) == 'number' then
	 features[i]:draw()
      end
   end
   gfx.setstate({blend="none"})
end
