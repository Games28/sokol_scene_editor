#pragma once
#ifndef CMN_AABB_STRUCT_H
#define CMN_AABB_STRUCT_H

namespace cmn {
	template<typename V2D>
	struct AABB_generic {
		V2D min=V2D(INFINITY, INFINITY), max=-min;

		V2D getCenter() const {
			return .5f*(min+max);
		}

		void fitToEnclose(const V2D& v) {
			if(v.x<min.x) min.x=v.x;
			if(v.y<min.y) min.y=v.y;
			if(v.x>max.x) max.x=v.x;
			if(v.y>max.y) max.y=v.y;
		}

		bool contains(const V2D& v) const {
			if(v.x<min.x||v.x>max.x) return false;
			if(v.y<min.y||v.y>max.y) return false;
			return true;
		}

		bool overlaps(const AABB_generic& o) const {
			if(min.x>o.max.x||max.x<o.min.x) return false;
			if(min.y>o.max.y||max.y<o.min.y) return false;
			return true;
		}
	};
}
#endif