
out = io.output()
fmt = string.format

tracker = new_tracker(40, 80)

features = {}

function features.add(self, idx, x, y, val)
   pt = { x=x, y=y, weight=val }

   function pt.lost(self, reason)
      out:write(fmt("point lost at %g,%g: %s\n",
		    self.x, self.y, reason))
   end

--[[
   function pt.move(self, x, y)
      out:write(fmt("pt move %g,%g -> %g,%g\n",
		    self.x, self.y, x, y))
      self.x = x
      self.y = y
   end
]]

   out:write(fmt("new point %d (%f, %f, %d)\n", 
		 idx, x, y, val))

   self[idx] = pt
end

function process_frame()
   out:write("----------------------------------------\n");
   tracker:track(features)
   out:write(fmt("tracker has %d features\n", tracker.active))
end
