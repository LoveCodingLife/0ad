#ifndef INCLUDED_STDSKELETONS
#define INCLUDED_STDSKELETONS

#include <string>

/*

Rough skeleton concept:

Different modelling/animation programs use different skeleton structures for
conceptually similar situations (e.g. humanoid bipeds). We'd like the game to
handle combinations of models and animations from different programs, so the
exported data must use a standard skeleton structure.

Rather than requiring awkward reconfiguration of those programs, we use an
XML file which defines a small set of standard skeletons (e.g. "biped") and
also a number of modelling-program-specific skeleton structures (e.g. "3ds Max
biped"). The non-standard ones define which bones we should expect to find,
and what standard bone they should get mapped onto. Sometimes multiple bones
will be mapped onto the same one (e.g. a series of fingers would all get mapped
onto a single hand bone), so the conversion is slightly lossy.

It is possible to have multiple independent standard skeletons - they just need
to be identified by using different bone names in the modelling program. The
game doesn't check that you're using models and animations with the same standard
skeleton, so that's up to the artists to take care of.

*/

struct Skeleton_impl;
class Skeleton
{
public:
	/** Default constructor - don't use; use FindSkeleton instead. */
	Skeleton();
	~Skeleton();

	/**
	 * Returns the number of bones in the standard-skeleton which this current
	 * skeleton is mapped onto.
	 */
	int GetBoneCount() const;

	/**
	 * Returns the ID number of the standard-skeleton bone, which corresponds
	 * to the named nonstandard-skeleton bone. Returns -1 if none is found.
	 * Can return the same value for multiple bones.
	 */
	int GetBoneID(const std::string& name) const;

	/**
	 * Similar to GetBoneID, but only returns a value for the most important (first)
	 * nonstandard-skeleton bone for each standard bone; returns -1 in other cases.
	 * This is necessary when deciding which nonstandard-skeleton-bone's animation
	 * should be saved. (TODO: think of a better name.)
	 */
	int GetRealBoneID(const std::string& name) const;

	/**
	 * Tries to find a skeleton that matches the given root bone name.
	 * Returns NULL if there is no match.
	 */
	static const Skeleton* FindSkeleton(const std::string& rootBoneName);

	/**
	 * Initialises the global state with skeleton data loaded from the
	 * given XML data. Must only be called once.
	 * (TODO: stop using global state.)
	 * @param xmlErrors output - XML parser error messages; will be non-empty
	 * on failure (and failure will always throw)
	 * @throws ColladaException on failure
	 */
	static void LoadSkeletonDataFromXml(const char* xmlData, size_t xmlLength, std::string& xmlErrors);

	std::auto_ptr<Skeleton_impl> m;
private:
	Skeleton(Skeleton&);
};

#endif // INCLUDED_STDSKELETONS
