import type { UveApi } from '../electron/preload';
import type {
    AnalysisResult, AppPrefs, Capabilities, JobSpec, JobState, LutLibraryState, MediaInfo, ToolsStatus, UpdateCheckResult, UserProfile, UveProject
} from '../shared/types';
import type { LutProfile } from '../shared/uve/lut';
import type { AppInfo } from './state/store';

declare global { interface Window { uve: UveApi } }

const i = <T>(ch: string, ...a: unknown[]) => window.uve.invoke(ch, ...a) as Promise<T>;

export interface FileFilter { name: string; extensions: string[] }
export interface PickedFile { name: string; path: string; data: Uint8Array }

export const api = {
    info: () => i<AppInfo>('app:info'),
    prefs: () => i<AppPrefs>('prefs:get'),
    setPrefs: (p: Partial<AppPrefs>) => i<AppPrefs>('prefs:set', p),
    caps: (force = false) => i<Capabilities>('caps:get', force),
    toolsStatus: () => i<ToolsStatus>('tools:status'),
    installTools: () => i<ToolsStatus>('tools:install'),
    checkUpdates: () => i<UpdateCheckResult>('updates:check'),
    applyUpdate: (id: string) => i<{ ok: boolean; message: string; restart?: boolean }>('updates:apply', id),
    skipUpdate: (id: string) => i<void>('updates:skip', id),
    openVideos: (multi = false) => i<string[]>('dialog:openVideos', multi),
    probe: (p: string) => i<MediaInfo>('media:probe', p),
    mediaUrl: (p: string) => i<string>('media:url', p),
    thumbs: (m: MediaInfo, count: number) => i<{ t: number; url: string }[]>('media:thumbs', m, count),
    proxy: (m: MediaInfo) => i<string>('media:proxy', m),
    analyze: (m: MediaInfo) => i<AnalysisResult>('analysis:run', m),
    clearCache: () => i<number>('cache:clear'),
    luts: () => i<LutLibraryState>('luts:get'),
    downloadLuts: (force = false) => i<LutLibraryState>('luts:download', force),
    saveLuts: (l: LutProfile[]) => i<LutLibraryState>('luts:save', l),
    deleteLuts: (keys: string[]) => i<LutLibraryState>('luts:delete', keys),
    restoreLuts: () => i<LutLibraryState>('luts:restore'),
    profiles: () => i<{ profiles: UserProfile[]; activeId: string | null }>('profiles:get'),
    saveProfiles: (d: { profiles: UserProfile[]; activeId: string | null }) => i<void>('profiles:save', d),
    filters: () => i<unknown[]>('filters:get'),
    saveFilters: (l: unknown[]) => i<void>('filters:save', l),
    session: () => i<unknown>('state:get'),
    saveSession: (s: unknown) => i<void>('state:set', s),
    openFiles: (title: string, filters: FileFilter[], multi = false, maxBytes?: number) => i<PickedFile[]>('files:open', { title, filters, multi, maxBytes }),
    saveFile: (title: string, defaultName: string, filters: FileFilter[], data: Uint8Array | string) => i<string | null>('files:save', { title, defaultName, filters, data }),
    unzip: (data: Uint8Array) => i<{ name: string; data: Uint8Array }[]>('files:unzip', data),
    pickFolder: (title: string) => i<string | null>('dialog:pickFolder', title),
    openProject: (p?: string) => i<{ path: string; project: UveProject; mediaOk: boolean; missing: string[] } | null>('project:open', p),
    saveProject: (path: string | null, project: UveProject, saveAs = false) => i<string | null>('project:save', { path, project, saveAs }),
    locateMedia: () => i<string | null>('project:locateMedia'),
    outputPath: (media: MediaInfo, exportSettings: JobSpec['exportSettings'], profileName: string) => i<string>('render:outputPath', { media, exportSettings, profileName }),
    enqueue: (spec: JobSpec) => i<JobState>('render:enqueue', spec),
    jobs: () => i<JobState[]>('render:list'),
    cancelJob: (id: string) => i<void>('render:cancel', id),
    retryJob: (id: string) => i<void>('render:retry', id),
    removeJob: (id: string) => i<void>('render:remove', id),
    clearFinished: () => i<void>('render:clearFinished'),
    showItem: (p: string) => i<void>('shell:showItem', p),
    openExternal: (u: string) => i<void>('shell:openExternal', u),
    setTitle: (t: string) => i<void>('window:setTitle', t),
    on: (ch: string, cb: (...a: any[]) => void) => window.uve.on(ch, cb), // eslint-disable-line @typescript-eslint/no-explicit-any
    pathForFile: (f: File) => window.uve.pathForFile(f)
};
