import { NextResponse } from 'next/server'
import { readFile, readdir } from 'fs/promises'
import { join } from 'path'

type Platform = 'win' | 'mac' | 'linux'

const MIME: Record<Platform, string> = {
  win:   'application/octet-stream',
  mac:   'application/x-apple-diskimage',
  linux: 'application/octet-stream',
}

const EXT: Record<Platform, string> = {
  win:   '.exe',
  mac:   '.dmg',
  linux: '.AppImage',
}

function detectPlatform(ua: string): Platform {
  if (/linux/i.test(ua) && !/android/i.test(ua)) return 'linux'
  if (/mac os x|macintosh/i.test(ua)) return 'mac'
  return 'win'
}

async function findLatest(dir: string, ext: string): Promise<string | null> {
  try {
    const files = (await readdir(dir))
      .filter(f => f.startsWith('PeerCam-Setup') && f.endsWith(ext))
      .sort()
      .reverse()
    return files[0] ?? null
  } catch { return null }
}

export async function GET(req: Request) {
  const { searchParams } = new URL(req.url)
  const override = searchParams.get('platform') as Platform | null
  const platform: Platform = (override && override in MIME) ? override : detectPlatform(req.headers.get('user-agent') ?? '')

  const downloadsDir = join(process.cwd(), 'public', 'downloads')
  const filename = await findLatest(downloadsDir, EXT[platform])

  if (!filename) {
    return NextResponse.json(
      { error: `No installer available for ${platform} yet. Check back after the next release.` },
      { status: 404 }
    )
  }

  const content = await readFile(join(downloadsDir, filename))
  return new NextResponse(content, {
    headers: {
      'Content-Type': MIME[platform],
      'Content-Disposition': `attachment; filename="${filename}"`,
      'Cache-Control': 'no-cache, no-store, must-revalidate',
    },
  })
}
