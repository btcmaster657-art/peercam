#!/usr/bin/env node
/**
 * build-save-desktop.mjs
 * Usage:
 *   node build-save-desktop.mjs           — build current platform, patch bump
 *   node build-save-desktop.mjs win       — Windows only
 *   node build-save-desktop.mjs mac       — macOS only
 *   node build-save-desktop.mjs linux     — Linux only
 *   node build-save-desktop.mjs minor     — minor version bump
 *   node build-save-desktop.mjs major     — major version bump
 *   node build-save-desktop.mjs --no-bump — build without version change
 *   node build-save-desktop.mjs --skip-native — skip native addon/DLL build
 */

import { execSync } from 'child_process'
import { readFileSync, writeFileSync, copyFileSync, readdirSync, rmSync, mkdirSync, existsSync } from 'fs'
import { join, dirname } from 'path'
import { fileURLToPath } from 'url'
import './lib/env.mjs'

const __dirname   = dirname(fileURLToPath(import.meta.url))
const DESKTOP_DIR = join(__dirname, 'desktop')
const PUBLIC_DIR  = join(__dirname, 'public')
const PKG_PATH    = join(DESKTOP_DIR, 'package.json')
const NATIVE_DIR  = join(DESKTOP_DIR, 'native')

const args     = process.argv.slice(2)
const bumpType = args.find(a => ['major', 'minor', 'patch'].includes(a)) ?? 'patch'
const noBump   = args.includes('--no-bump')
const skipNative = args.includes('--skip-native')

const PLATFORM_ARGS = ['win', 'mac', 'linux']
const requestedPlatforms = args.filter(a => PLATFORM_ARGS.includes(a))
const defaultPlatform = process.platform === 'darwin' ? 'mac'
  : process.platform === 'linux' ? 'linux' : 'win'
const platforms = requestedPlatforms.length > 0 ? requestedPlatforms : [defaultPlatform]

// ── Version bump ──────────────────────────────────────────────────────────────
const pkg = JSON.parse(readFileSync(PKG_PATH, 'utf-8'))
const currentVersion = pkg.version
const [major, minor, patch] = currentVersion.split('.').map(Number)

let newVersion = currentVersion
if (!noBump) {
  if (bumpType === 'major')      newVersion = `${major + 1}.0.0`
  else if (bumpType === 'minor') newVersion = `${major}.${minor + 1}.0`
  else                           newVersion = `${major}.${minor}.${patch + 1}`
  pkg.version = newVersion
  writeFileSync(PKG_PATH, JSON.stringify(pkg, null, 2))
  console.log(`\n  Version: ${currentVersion} → ${newVersion}`)
} else {
  console.log(`\n  Version: ${currentVersion} (no bump)`)
}

// ── Native builds (Windows only) ─────────────────────────────────────────────
if (platforms.includes('win') && !skipNative) {
  // 1. Build DirectShow filter DLL
  const dllPath = join(NATIVE_DIR, 'vcam_filter', 'PeerCamVCam.dll')
  console.log('\n  Building PeerCamVCam.dll (DirectShow virtual camera filter)...')
  try {
    execSync(`call "${join(NATIVE_DIR, 'vcam_filter', 'build.bat')}"`, {
      cwd: join(NATIVE_DIR, 'vcam_filter'),
      stdio: 'inherit',
      shell: 'cmd.exe',
    })
  } catch {
    console.error('\n  DLL build failed!')
    process.exit(1)
  }
  if (!existsSync(dllPath)) {
    console.error('\n  ERROR: PeerCamVCam.dll not found after build')
    process.exit(1)
  }
  console.log('  ✓ PeerCamVCam.dll built')

  // 2. Build Node addon (vcam.node)
  const addonPath = join(NATIVE_DIR, 'build', 'Release', 'vcam.node')
  console.log('\n  Building vcam.node (Node native addon)...')
  try {
    execSync(`call "${join(NATIVE_DIR, 'build_addon.bat')}"`, {
      cwd: NATIVE_DIR,
      stdio: 'inherit',
      shell: 'cmd.exe',
    })
  } catch {
    console.error('\n  vcam.node build failed!')
    process.exit(1)
  }
  if (!existsSync(addonPath)) {
    console.error('\n  ERROR: vcam.node not found after build')
    process.exit(1)
  }
  console.log('  ✓ vcam.node built')
} else if (platforms.includes('win') && skipNative) {
  // Verify pre-built artifacts exist
  const dllPath   = join(NATIVE_DIR, 'vcam_filter', 'PeerCamVCam.dll')
  const addonPath = join(NATIVE_DIR, 'build', 'Release', 'vcam.node')
  if (!existsSync(dllPath))   { console.error('\n  ERROR: PeerCamVCam.dll missing — run without --skip-native first'); process.exit(1) }
  if (!existsSync(addonPath)) { console.error('\n  ERROR: vcam.node missing — run without --skip-native first'); process.exit(1) }
  console.log('\n  ✓ Using pre-built native artifacts (--skip-native)')
}

// ── Clear release dir ─────────────────────────────────────────────────────────
const releaseDir = join(DESKTOP_DIR, 'release')
try { rmSync(releaseDir, { recursive: true, force: true }); console.log('\n  ✓ Cleared desktop/release cache') } catch {}
mkdirSync(join(PUBLIC_DIR, 'downloads'), { recursive: true })

// ── Electron-builder ──────────────────────────────────────────────────────────
const PLATFORM_CONFIG = {
  win:   { script: 'pack:win',   find: f => f === 'PeerCam-Setup.exe',      dest: `PeerCam-Setup_${newVersion}.exe` },
  mac:   { script: 'pack:mac',   find: f => f === 'PeerCam-Setup.dmg',      dest: `PeerCam-Setup_${newVersion}.dmg` },
  linux: { script: 'pack:linux', find: f => f === 'PeerCam-Setup.AppImage', dest: `PeerCam-Setup_${newVersion}.AppImage` },
}

for (const platform of platforms) {
  const { script, find, dest } = PLATFORM_CONFIG[platform]
  console.log(`\n  Building ${platform} installer (v${newVersion})...`)
  try {
    execSync(`npm run ${script}`, { cwd: DESKTOP_DIR, stdio: 'inherit' })
  } catch {
    console.error(`\n  Build failed for ${platform}!`)
    process.exit(1)
  }
  const artifact = readdirSync(releaseDir).find(find)
  if (!artifact) { console.error(`\n  ERROR: No ${platform} artifact found in desktop/release/`); process.exit(1) }
  copyFileSync(join(releaseDir, artifact), join(PUBLIC_DIR, 'downloads', dest))
  console.log(`  ✓ ${artifact} → public/downloads/${dest}`)
}

console.log(`\n  ✓ Version: ${newVersion}`)
console.log(`  Deploy with: npx vercel --prod\n`)
