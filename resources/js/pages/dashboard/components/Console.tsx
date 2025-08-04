import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Button } from '@/components/ui/button';
import { Dialog, DialogClose, DialogContent, DialogHeader, DialogTitle } from '@/components/ui/dialog';
import { Collapsible, CollapsibleContent, CollapsibleTrigger } from '@/components/ui/collapsible';
import { 
  Clock, 
  ChevronDown, 
  ChevronRight, 
  Copy, 
  XIcon
} from 'lucide-react';
import { useRef, useEffect, useState, useCallback } from 'react';
import { toast } from 'sonner';

interface ConsoleProps {
  logs: string[];
  status?: 'ready' | 'loading' | 'error';
  variant?: 'accordion' | 'dialog';
  open?: boolean;
  onOpenChange?: (open: boolean) => void;
}

// Status indicator component
export function StatusIndicator({ status }: { status: 'ready' | 'loading' | 'error' }) {
  const baseClass = "w-2 h-2 rounded-full";
  
  switch (status) {
    case 'ready':
      return <div className={`${baseClass} bg-green-500`} />;
    case 'loading':
      return <div className={`${baseClass} bg-yellow-500 animate-pulse`} />;
    case 'error':
      return <div className={`${baseClass} bg-red-500`} />;
    default:
      return <div className={`${baseClass} bg-gray-500`} />;
  }
}

// Console logs display component
function ConsoleDisplay({ logs, className }: { logs: string[]; className?: string }) {
  const consoleBodyRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    if (consoleBodyRef.current) {
      consoleBodyRef.current.scrollTop = consoleBodyRef.current.scrollHeight;
    }
  }, [logs]);

  return (
    <div
      ref={consoleBodyRef}
      className={`overflow-y-auto rounded-lg bg-slate-950 p-4 font-mono text-sm border ${className}`}
    >
      {logs.length === 0 ? (
        <p className="text-slate-400 italic">No logs yet...</p>
      ) : (
        logs.map((log, index) => (
          <div key={index} className="mb-1 flex">
            <span className="text-slate-500 mr-3 select-none min-w-[3rem]">
              {String(index + 1).padStart(3, '0')}
            </span>
            <span className="text-slate-200 whitespace-pre-wrap break-words">
              {log}
            </span>
          </div>
        ))
      )}
    </div>
  );
}

// Console as a dialog for larger screens
function ConsoleDialog({ 
  logs, 
  open, 
  onOpenChange, 
  status = 'ready' 
}: { 
  logs: string[]; 
  open: boolean; 
  onOpenChange: (open: boolean) => void;
  status: 'ready' | 'loading' | 'error';
}) {
  const copyLogs = useCallback(() => {
    const logsText = logs.join('\n');
    navigator.clipboard.writeText(logsText).then(() => {
      toast.success("Logs copied to clipboard");
    }).catch(() => {
      toast.error("Failed to copy logs to clipboard");
    });
  }, [logs]);

  return (
    <Dialog open={open} onOpenChange={onOpenChange}>
        <DialogContent showDefaultCloseButton={false} className="min-w-[90vw] max-h-[80vh] flex flex-col p-0 overflow-hidden">
            <DialogHeader>
                <div className="flex items-center gap-2 flex-1">
                    <StatusIndicator status={status} />
                    <Clock className="h-5 w-5" />
                    <DialogTitle>Console</DialogTitle>
                </div>
                <div className="flex items-center gap-2">
                    <Button variant="outline" size="sm" onClick={copyLogs}>
                    <Copy className="h-4 w-4 mr-2" />
                    Copy All
                    </Button>
                    <DialogClose asChild>
                    <Button size="icon" variant="ghost" className="text-xl p-2">
                        <XIcon className="w-6 h-6" />
                        <span className="sr-only">Close</span>
                    </Button>
                    </DialogClose>
                </div>
            </DialogHeader>
            <div className="flex-1 min-h-0 px-6 py-4 overflow-auto">
                <ConsoleDisplay logs={logs} />
            </div>
        </DialogContent>
    </Dialog>
  );
}

// Console as an accordion for smaller screens
function ConsoleAccordion({ 
  logs, 
  status = 'ready' 
}: { 
  logs: string[]; 
  status: 'ready' | 'loading' | 'error';
}) {
  const [isOpen, setIsOpen] = useState(false);

  const copyLogs = useCallback(() => {
    const logsText = logs.join('\n');
    navigator.clipboard.writeText(logsText).then(() => {
      toast.success("Logs copied to clipboard");
    }).catch(() => {
      toast.error("Failed to copy logs to clipboard");
    });
  }, [logs]);

  return (
    <Card>
      <Collapsible open={isOpen} onOpenChange={setIsOpen}>
        <CollapsibleTrigger asChild>
          <CardHeader className="cursor-pointer transition-colors">
            <CardTitle className="flex items-center justify-between">
              <div className="flex items-center gap-2">
                <StatusIndicator status={status} />
                <Clock className="h-5 w-5" />
                Console
              </div>
              <div className="flex items-center gap-2">
                <Button
                  variant="outline"
                  size="sm"
                  onClick={(e) => {
                    e.stopPropagation();
                    copyLogs();
                  }}
                >
                  <Copy className="h-4 w-4 mr-2" />
                  Copy All
                </Button>
                <Button
                    variant="outline"
                    size="sm"
                >
                    {isOpen ? (
                        <ChevronDown className="h-4 w-4"/>
                    ) : (
                        <ChevronRight className="h-4 w-4" />
                    )}
                </Button>
              </div>
            </CardTitle>
          </CardHeader>
        </CollapsibleTrigger>
        <CollapsibleContent>
          <CardContent className='pt-4'>
            <ConsoleDisplay logs={logs} className="h-64" />
          </CardContent>
        </CollapsibleContent>
      </Collapsible>
    </Card>
  );
}

// Main Console component that renders based on variant
export function Console({ 
  logs, 
  status = 'ready', 
  variant = 'accordion', 
  open = false, 
  onOpenChange 
}: ConsoleProps) {
  if (variant === 'dialog' && onOpenChange) {
    return (
      <ConsoleDialog
        logs={logs}
        open={open}
        onOpenChange={onOpenChange}
        status={status}
      />
    );
  }

  return <ConsoleAccordion logs={logs} status={status} />;
}