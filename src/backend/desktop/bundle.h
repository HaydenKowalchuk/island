#ifndef ISLAND_BUNDLE_H
#define ISLAND_BUNDLE_H

/*
 * macOS app-bundle asset discovery (island1-c990).
 *
 * When island runs as a double-clicked .app there is no ASSET_DIR in the
 * environment and the working directory is useless ("/"), so the loaders'
 * relative "assets" fallback cannot find the bundled art + sound. This
 * helper resolves the bundle's own Contents/Resources/assets directory from
 * the executable path (island.app/Contents/MacOS/island -> ../Resources/assets).
 *
 * Deliberately dormant outside a bundle: it only ever returns non-NULL on
 * __APPLE__ and only when the binary really sits in a /Contents/MacOS/ layout.
 * A plain dev build (build/src/island, run.sh with ASSET_DIR) always gets
 * NULL, so the existing env-var -> relative-"assets" flow is untouched.
 */

const char* bundle_assets_root(void);

#endif /* ISLAND_BUNDLE_H */
