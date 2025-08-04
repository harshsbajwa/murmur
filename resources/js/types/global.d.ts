import type { Config } from 'ziggy-js';

interface Router {
    current(): string | undefined;
    current(name: string, params?: unknown): boolean;
    get params(): Record<string, string>;
    get routeParams(): Record<string, string>;
    get queryParams(): Record<string, unknown>;
    has(name: string): boolean;
}

declare global {
    function route(): Router;
    function route(name: undefined, params: undefined, absolute?: boolean, config?: Config): Router;
    function route(name: string, params?: unknown, absolute?: boolean, config?: Config): string;
}
