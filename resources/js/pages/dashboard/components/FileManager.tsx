import React, { useState, useRef } from 'react';
import {
  ColumnDef,
  ColumnFiltersState,
  flexRender,
  getCoreRowModel,
  getFilteredRowModel,
  getPaginationRowModel,
  getSortedRowModel,
  SortingState,
  useReactTable,
  VisibilityState,
} from "@tanstack/react-table";
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Button } from '@/components/ui/button';
import { Badge } from '@/components/ui/badge';
import { Checkbox } from '@/components/ui/checkbox';
import { Input } from '@/components/ui/input';
import {
  Dialog,
  DialogContent,
  DialogHeader,
  DialogTitle,
  DialogTrigger,
} from '@/components/ui/dialog';
import {
  DropdownMenu,
  DropdownMenuCheckboxItem,
  DropdownMenuContent,
  DropdownMenuItem,
  DropdownMenuLabel,
  DropdownMenuSeparator,
  DropdownMenuTrigger,
} from '@/components/ui/dropdown-menu';
import {
  Table,
  TableBody,
  TableCell,
  TableHead,
  TableHeader,
  TableRow,
} from '@/components/ui/table';
import { 
  FileVideo, 
  Upload, 
  Save, 
  Download, 
  Trash2, 
  Share2, 
  Files, 
  Folder, 
  Table as TableIcon,
  ArrowUpDown,
  ArrowUp,
  ArrowDown,
  ChevronDown,
  MoreHorizontal,
  X
} from 'lucide-react';
import { VideoFile } from '@/types';

const formatFileSize = (bytes: number): string => {
    if (bytes === 0) return '0 Bytes';
    const k = 1024;
    const sizes = ['Bytes', 'KiB', 'MiB', 'GiB'];
    const i = Math.floor(Math.log(bytes) / Math.log(k));
    return `${parseFloat((bytes / Math.pow(k, i)).toFixed(2))} ${sizes[i]}`;
};

const formatDate = (date: string | Date): string => {
    return new Date(date).toLocaleDateString('en-US', {
        year: 'numeric',
        month: 'short',
        day: 'numeric',
        hour: '2-digit',
        minute: '2-digit'
    });
};

interface FileManagerProps {
    files: VideoFile[];
    currentFileId: string | null;
    onFileSelect: (file: VideoFile) => void;
    onFileSave: (file: VideoFile) => void;
    onFileDelete: (fileId: string) => void;
    onFileShare: (file: VideoFile) => void;
    onFileUpload?: (files: FileList) => void;
}

interface FileTableDialogProps {
    files: VideoFile[];
    currentFileId: string | null;
    onFileSelect: (file: VideoFile) => void;
    onFileSave: (file: VideoFile) => void;
    onFileDelete: (fileId: string) => void;
    onFileShare: (file: VideoFile) => void;
    onBulkDelete: (fileIds: string[]) => void;
    onBulkSave: (files: VideoFile[]) => void;
    onFileUpload?: (files: FileList) => void;
}

function FileTableDialog({
    files,
    currentFileId,
    onFileSelect,
    onFileSave,
    onFileDelete,
    onFileShare,
    onBulkDelete,
    onBulkSave,
    onFileUpload
}: FileTableDialogProps) {
    const [sorting, setSorting] = useState<SortingState>([]);
    const [columnFilters, setColumnFilters] = useState<ColumnFiltersState>([]);
    const [columnVisibility, setColumnVisibility] = useState<VisibilityState>({});
    const [rowSelection, setRowSelection] = useState({});
    const [isOpen, setIsOpen] = useState(false);
    const fileInputRef = useRef<HTMLInputElement>(null);

    const handleDownload = (e: React.MouseEvent, file: VideoFile) => {
        e.stopPropagation();
        const a = document.createElement('a');
        a.href = file.url;
        a.download = file.name;
        document.body.appendChild(a);
        a.click();
        document.body.removeChild(a);
    };

    const handleBulkAction = (action: 'delete' | 'save') => {
        const selectedFiles = files.filter((_, index) => rowSelection[index]);
        if (action === 'delete') {
            onBulkDelete(selectedFiles.map(f => f.id));
        } else {
            onBulkSave(selectedFiles.filter(f => !f.saved));
        }
        setRowSelection({});
    };

    const handleUploadClick = () => {
        fileInputRef.current?.click();
    };

    const handleFileInputChange = (e: React.ChangeEvent<HTMLInputElement>) => {
        const files = e.target.files;
        if (files && onFileUpload) {
            onFileUpload(files);
        }
        // Reset the input value so the same file can be selected again
        e.target.value = '';
    };

    const columns: ColumnDef<VideoFile>[] = [
        {
            id: "select",
            header: ({ table }) => (
                <div className="flex items-center justify-center">
                    <Checkbox
                        checked={
                            table.getIsAllPageRowsSelected() ||
                            (table.getIsSomePageRowsSelected() && "indeterminate")
                        }
                        onCheckedChange={(value) => table.toggleAllPageRowsSelected(!!value)}
                        aria-label="Select all"
                    />
                </div>
            ),
            cell: ({ row }) => (
                <div className="flex items-center justify-center">
                    <Checkbox
                        checked={row.getIsSelected()}
                        onCheckedChange={(value) => row.toggleSelected(!!value)}
                        aria-label="Select row"
                    />
                </div>
            ),
            enableSorting: false,
            enableHiding: false,
            size: 40,
        },
        {
            accessorKey: "name",
            header: ({ column }) => {
                const sortDirection = column.getIsSorted();
                return (
                    <div className="text-left">
                        <Button
                            variant="ghost"
                            className='hover:bg-inherit'
                            onClick={() => column.toggleSorting(column.getIsSorted() === "asc")}
                        >
                            <span className="font-semibold -ml-3">Name</span>
                            {sortDirection === "asc" ? (
                                <ArrowUp className="ml-2 h-4 w-4" />
                            ) : sortDirection === "desc" ? (
                                <ArrowDown className="ml-2 h-4 w-4" />
                            ) : (
                                <ArrowUpDown className="ml-2 h-4 w-4 opacity-50" />
                            )}
                        </Button>
                    </div>
                );
            },
            cell: ({ row }) => {
                const file = row.original;
                return (
                    <div className="flex items-center gap-2 text-left">
                        <div className="min-w-0">
                            <p className="font-medium truncate" title={file.name}>
                                {file.name}
                            </p>
                            {currentFileId === file.id && (
                                <Badge variant="outline" className="text-xs mt-1">
                                    Current
                                </Badge>
                            )}
                        </div>
                    </div>
                );
            },
        },
        {
            accessorKey: "size",
            header: ({ column }) => {
                const sortDirection = column.getIsSorted();
                return (
                    <div className="text-right">
                        <Button
                            variant="ghost"
                            className='hover:bg-inherit'
                            onClick={() => column.toggleSorting(column.getIsSorted() === "asc")}
                        >
                            {sortDirection === "asc" ? (
                                <ArrowUp className="mr-2 h-4 w-4" />
                            ) : sortDirection === "desc" ? (
                                <ArrowDown className="mr-2 h-4 w-4" />
                            ) : (
                                <ArrowUpDown className="mr-2 h-4 w-4 opacity-50" />
                            )}
                            <span className="font-semibold -mr-3">Size</span>
                        </Button>
                    </div>
                );
            },
            cell: ({ row }) => {
                const size = row.getValue("size") as number;
                return <div className="text-sm text-right">{formatFileSize(size)}</div>;
            },
            size: 100,
        },
        {
            accessorKey: "type",
            header: ({ column }) => (
                <div className="text-right font-semibold">Type</div>
            ),
            cell: ({ row }) => {
                const type = row.getValue("type") as string;
                return (
                    <div className="flex justify-end">
                        <Badge variant="secondary">{type}</Badge>
                    </div>
                );
            },
            size: 120,
        },
        {
            accessorKey: "saved",
            header: ({ column }) => (
                <div className="text-center font-semibold">Status</div>
            ),
            cell: ({ row }) => {
                const saved = row.getValue("saved") as boolean;
                return (
                    <div className="flex justify-center">
                        {saved ? (
                            <Badge variant="default" className="bg-green-100 text-green-800">
                                <Save className="h-3 w-3 mr-1" />
                                Saved
                            </Badge>
                        ) : (
                            <Badge variant="outline">
                                Unsaved
                            </Badge>
                        )}
                    </div>
                );
            },
            size: 100,
        },
        {
            accessorKey: "lastModified",
            header: ({ column }) => {
                const sortDirection = column.getIsSorted();
                return (
                    <div className="text-right">
                        <Button
                            variant="ghost"
                            className='hover:bg-inherit'
                            onClick={() => column.toggleSorting(column.getIsSorted() === "asc")}
                        >
                            {sortDirection === "asc" ? (
                                <ArrowUp className="mr-2 h-4 w-4" />
                            ) : sortDirection === "desc" ? (
                                <ArrowDown className="mr-2 h-4 w-4" />
                            ) : (
                                <ArrowUpDown className="mr-2 h-4 w-4 opacity-50" />
                            )}
                            <span className="font-semibold -mr-3">Last Modified</span>
                        </Button>
                    </div>
                );
            },
            cell: ({ row }) => {
                const date = row.getValue("lastModified") as number;
                return <div className="text-sm text-right">{formatDate(new Date(date))}</div>;
            },
            size: 100,
        },
        {
            id: "actions",
            header: ({ column }) => (
                <div className="text-center font-semibold">Actions</div>
            ),
            enableHiding: false,
            cell: ({ row }) => {
                const file = row.original;
                return (
                    <div className="flex justify-center">
                        <DropdownMenu>
                            <DropdownMenuTrigger asChild>
                                <Button variant="ghost" className="h-8 w-8 p-0">
                                    <span className="sr-only">Open menu</span>
                                    <MoreHorizontal className="h-4 w-4" />
                                </Button>
                            </DropdownMenuTrigger>
                            <DropdownMenuContent align="end">
                                {!file.saved && (
                                    <DropdownMenuItem onClick={() => onFileSave(file)}>
                                        <Save className="h-4 w-4 mr-2" />
                                        Save
                                    </DropdownMenuItem>
                                )}
                                <DropdownMenuItem onClick={() => onFileShare(file)}>
                                    <Share2 className="h-4 w-4 mr-2" />
                                    Share
                                </DropdownMenuItem>
                                <DropdownMenuItem onClick={(e) => handleDownload(e, file)}>
                                    <Download className="h-4 w-4 mr-2" />
                                    Download
                                </DropdownMenuItem>
                                <DropdownMenuSeparator />
                                <DropdownMenuItem 
                                    onClick={() => onFileDelete(file.id)}
                                    className="text-red-600"
                                >
                                    <Trash2 className="h-4 w-4 mr-2" />
                                    Delete
                                </DropdownMenuItem>
                            </DropdownMenuContent>
                        </DropdownMenu>
                    </div>
                );
            },
            size: 80,
        },
    ];

    const table = useReactTable({
        data: files,
        columns,
        onSortingChange: setSorting,
        onColumnFiltersChange: setColumnFilters,
        getCoreRowModel: getCoreRowModel(),
        getPaginationRowModel: getPaginationRowModel(),
        getSortedRowModel: getSortedRowModel(),
        getFilteredRowModel: getFilteredRowModel(),
        onColumnVisibilityChange: setColumnVisibility,
        onRowSelectionChange: setRowSelection,
        state: {
            sorting,
            columnFilters,
            columnVisibility,
            rowSelection,
        },
    });

    const selectedRowCount = table.getFilteredSelectedRowModel().rows.length;

    return (
        <Dialog open={isOpen} onOpenChange={setIsOpen}>
            <DialogTrigger asChild>
                <Button size="sm" variant="outline">
                    <TableIcon className="h-3 w-3 md:mr-2" />
                    All Files
                </Button>
            </DialogTrigger>
            <DialogContent className="min-w-[90vw] max-w-[95vw] max-h-[90vh] h-[90vh] flex flex-col p-0">
                <DialogHeader className="px-4 py-3 border-b flex-shrink-0">
                    <DialogTitle className="flex items-center gap-2 text-lg font-semibold">
                        <TableIcon className="h-5 w-5" />
                        All Files ({files.length})
                    </DialogTitle>
                </DialogHeader>
                
                {/* Hidden file input */}
                <input
                    ref={fileInputRef}
                    type="file"
                    multiple
                    accept="video/*"
                    onChange={handleFileInputChange}
                    className="hidden"
                />
                
                {/* Filters and Controls */}
                <div className="flex-shrink-0 px-4 py-3 border-b">
                    <div className="flex items-center justify-between gap-4">
                        <div className="flex gap-3">
                            <Input
                                placeholder="Search..."
                                value={(table.getColumn("name")?.getFilterValue() as string) ?? ""}
                                onChange={(event) =>
                                    table.getColumn("name")?.setFilterValue(event.target.value)
                                }
                                className="max-w-sm h-8"
                            />
                            {selectedRowCount > 0 && (
                                <div className="flex items-center gap-2">
                                    <Badge variant="secondary" className="text-xs h-6 flex items-center">
                                        {selectedRowCount} selected
                                    </Badge>
                                    <Button
                                        size="sm"
                                        variant="outline"
                                        onClick={() => handleBulkAction('save')}
                                        className="h-8 px-2 text-xs flex items-center"
                                    >
                                        <Save className="h-3 w-3 mr-1" />
                                        Save
                                    </Button>
                                    <Button
                                        size="sm"
                                        variant="destructive"
                                        onClick={() => handleBulkAction('delete')}
                                        className="h-8 px-2 text-xs flex items-center"
                                    >
                                        <Trash2 className="h-3 w-3 mr-1" />
                                        Delete
                                    </Button>
                                </div>
                            )}
                        </div>
                        <div className="flex items-center gap-2">
                            {onFileUpload && (
                                <Button
                                    variant="outline"
                                    size="sm"
                                    onClick={handleUploadClick}
                                    className="h-8 px-2 text-xs flex items-center"
                                >
                                    <Upload className="h-3 w-3 mr-1" />
                                    Upload
                                </Button>
                            )}
                            <DropdownMenu>
                                <DropdownMenuTrigger asChild>
                                    <Button
                                        variant="outline"
                                        size="sm"
                                        className="h-8 px-2 text-xs flex items-center"
                                    >
                                        Columns <ChevronDown className="ml-1 h-3 w-3" />
                                    </Button>
                                </DropdownMenuTrigger>
                                <DropdownMenuContent align="end">
                                    {table
                                        .getAllColumns()
                                        .filter((column) => column.getCanHide())
                                        .map((column) => (
                                            <DropdownMenuCheckboxItem
                                                key={column.id}
                                                className="capitalize"
                                                checked={column.getIsVisible()}
                                                onCheckedChange={(value) =>
                                                    column.toggleVisibility(!!value)
                                                }
                                            >
                                                {column.id}
                                            </DropdownMenuCheckboxItem>
                                        ))}
                                </DropdownMenuContent>
                            </DropdownMenu>
                        </div>
                    </div>
                </div>

                {/* Table Container */}
                <div className="flex-1 min-h-0 relative">
                    <div className="absolute inset-0 overflow-auto">
                        <Table>
                            <TableHeader className="sticky top-0 z-10 bg-background">
                                {table.getHeaderGroups().map((headerGroup) => (
                                    <TableRow key={headerGroup.id}>
                                        {headerGroup.headers.map((header) => (
                                            <TableHead 
                                                key={header.id}
                                                className="bg-background"
                                                style={{ 
                                                    width: header.getSize() !== 150 ? header.getSize() : undefined 
                                                }}
                                            >
                                                {header.isPlaceholder
                                                    ? null
                                                    : flexRender(
                                                          header.column.columnDef.header,
                                                          header.getContext()
                                                      )}
                                            </TableHead>
                                        ))}
                                    </TableRow>
                                ))}
                            </TableHeader>
                            <TableBody>
                                {table.getRowModel().rows?.length ? (
                                    table.getRowModel().rows.map((row) => (
                                        <TableRow
                                            key={row.id}
                                            data-state={row.getIsSelected() && "selected"}
                                            className="hover:bg-muted/50 cursor-pointer h-12"
                                            onClick={() => onFileSelect(row.original)}
                                        >
                                            {row.getVisibleCells().map((cell) => (
                                                <TableCell 
                                                    key={cell.id} 
                                                    className="py-2"
                                                    style={{ 
                                                        width: cell.column.getSize() !== 150 ? cell.column.getSize() : undefined 
                                                    }}
                                                >
                                                    {flexRender(
                                                        cell.column.columnDef.cell,
                                                        cell.getContext()
                                                    )}
                                                </TableCell>
                                            ))}
                                        </TableRow>
                                    ))
                                ) : (
                                    <TableRow className="hover:bg-background">
                                        <TableCell
                                            colSpan={columns.length}
                                            className="h-48 text-center"
                                        >
                                            <div className="flex flex-col items-center justify-center py-8 text-muted-foreground">
                                                <h3 className="text-xl font-medium mb-2">No files found</h3>
                                                <p className="text-sm">Upload your first video file to get started</p>
                                            </div>
                                        </TableCell>
                                    </TableRow>
                                )}
                            </TableBody>
                        </Table>
                    </div>
                </div>

                {/* Pagination */}
                <div className="flex-shrink-0 flex items-center justify-between px-4 py-3 border-t text-sm">
                    <div className="text-muted-foreground">
                        {selectedRowCount} of {table.getFilteredRowModel().rows.length} row(s) selected.
                    </div>
                    <div className="flex items-center space-x-6">
                        <div className="flex items-center space-x-2">
                            <span className="text-sm font-medium">Rows per page</span>
                            <select
                                value={table.getState().pagination.pageSize}
                                onChange={(e) => {
                                    table.setPageSize(Number(e.target.value));
                                }}
                                className="h-8 w-[70px] rounded border border-input px-2 text-sm bg-background"
                            >
                                {[10, 20, 30, 40, 50].map((pageSize) => (
                                    <option key={pageSize} value={pageSize}>
                                        {pageSize}
                                    </option>
                                ))}
                            </select>
                        </div>
                        <div className="flex items-center space-x-4">
                            <div className="flex w-[100px] items-center justify-center text-sm font-medium">
                                Page {table.getState().pagination.pageIndex + 1} of{" "}
                                {table.getPageCount() || 1}
                            </div>
                            <div className="flex items-center space-x-2">
                                <Button
                                    variant="outline"
                                    size="sm"
                                    onClick={() => table.setPageIndex(0)}
                                    disabled={!table.getCanPreviousPage()}
                                    className="h-8 px-2"
                                >
                                    {"<<"}
                                </Button>
                                <Button
                                    variant="outline"
                                    size="sm"
                                    onClick={() => table.previousPage()}
                                    disabled={!table.getCanPreviousPage()}
                                    className="h-8 px-2"
                                >
                                    {"<"}
                                </Button>
                                <Button
                                    variant="outline"
                                    size="sm"
                                    onClick={() => table.nextPage()}
                                    disabled={!table.getCanNextPage()}
                                    className="h-8 px-2"
                                >
                                    {">"}
                                </Button>
                                <Button
                                    variant="outline"
                                    size="sm"
                                    onClick={() => table.setPageIndex(table.getPageCount() - 1)}
                                    disabled={!table.getCanNextPage()}
                                    className="h-8 px-2"
                                >
                                    {">>"}
                                </Button>
                            </div>
                        </div>
                    </div>
                </div>
            </DialogContent>
        </Dialog>
    );
}

export function FileManager({
    files,
    currentFileId,
    onFileSelect,
    onFileSave,
    onFileDelete,
    onFileShare,
    onFileUpload,
}: FileManagerProps) {
    const fileInputRef = useRef<HTMLInputElement>(null);

    // Get the 3 most recently uploaded files based on lastModified timestamp
    const recentFiles = [...files]
        .sort((a, b) => b.lastModified - a.lastModified)
        .slice(0, 3);

    const handleDownload = (e: React.MouseEvent, file: VideoFile) => {
        e.stopPropagation();
        const a = document.createElement('a');
        a.href = file.url;
        a.download = file.name;
        document.body.appendChild(a);
        a.click();
        document.body.removeChild(a);
    };

    const handleBulkDelete = (fileIds: string[]) => {
        fileIds.forEach(id => onFileDelete(id));
    };

    const handleBulkSave = (files: VideoFile[]) => {
        files.forEach(file => onFileSave(file));
    };

    const handleUploadClick = () => {
        fileInputRef.current?.click();
    };

    const handleFileInputChange = (e: React.ChangeEvent<HTMLInputElement>) => {
        const files = e.target.files;
        if (files && onFileUpload) {
            onFileUpload(files);
        }
        // Reset the input value so the same file can be selected again
        e.target.value = '';
    };

    return (
        <Card>
            <CardHeader>
                <CardTitle className="flex items-center justify-between">
                    <div className="flex items-center gap-2">
                        <Folder className="h-5 w-5" />
                        Files
                        {files.length > 3 && (
                            <Badge variant="secondary" className="text-xs">
                                {files.length - 3} more
                            </Badge>
                        )}
                    </div>
                    <div className="flex items-center gap-2">
                        {onFileUpload && (
                            <>
                                <input
                                    ref={fileInputRef}
                                    type="file"
                                    multiple
                                    accept="video/*"
                                    onChange={handleFileInputChange}
                                    className="hidden"
                                />
                                <Button size="sm" variant="outline" onClick={handleUploadClick}>
                                    <Upload className="h-3 w-3 md:mr-2" />
                                    Upload
                                </Button>
                            </>
                        )}
                        <FileTableDialog
                            files={files}
                            currentFileId={currentFileId}
                            onFileSelect={onFileSelect}
                            onFileSave={onFileSave}
                            onFileDelete={onFileDelete}
                            onFileShare={onFileShare}
                            onBulkDelete={handleBulkDelete}
                            onBulkSave={handleBulkSave}
                            onFileUpload={onFileUpload}
                        />
                    </div>
                </CardTitle>
            </CardHeader>
            <CardContent>
                {recentFiles.length > 0 ? (
                    <div className="space-y-2">
                        {recentFiles.map((file) => (
                            <div
                                key={file.id}
                                className="flex cursor-pointer flex-col gap-3 rounded-lg border p-3 transition-colors sm:flex-row sm:items-center sm:justify-between hover:bg-muted/50"
                                onClick={() => onFileSelect(file)}
                            >
                                <div className="flex min-w-0 items-center gap-3">
                                    <FileVideo className="h-5 w-5 flex-shrink-0 text-muted-foreground" />
                                    <div className="min-w-0">
                                        <p className="truncate font-medium" title={file.name}>
                                            {file.name}
                                        </p>
                                        <p className="text-sm text-muted-foreground">
                                            {formatFileSize(file.size)} • {file.type}
                                        </p>
                                    </div>
                                </div>
                                <div className="flex flex-shrink-0 items-center gap-2 self-end sm:self-auto">
                                    {file.saved && (
                                        <Badge variant="secondary">
                                            <Save className="h-3 w-3" />
                                            <span className="ml-1">Saved</span>
                                        </Badge>
                                    )}
                                    {!file.saved && (
                                        <Button
                                            size="sm"
                                            variant="outline"
                                            onClick={(e) => {
                                                e.stopPropagation();
                                                onFileSave(file);
                                            }}
                                        >
                                            <Save className="h-3 w-3" />
                                        </Button>
                                    )}
                                    <Button
                                        size="sm"
                                        variant="outline"
                                        onClick={(e) => {
                                            e.stopPropagation();
                                            onFileShare(file);
                                        }}
                                    >
                                        <Share2 className="h-3 w-3" />
                                    </Button>
                                    <Button size="sm" variant="outline" onClick={(e) => handleDownload(e, file)}>
                                        <Download className="h-3 w-3" />
                                    </Button>
                                    <Button
                                        size="sm"
                                        variant="destructive"
                                        onClick={(e) => {
                                            e.stopPropagation();
                                            onFileDelete(file.id);
                                        }}
                                    >
                                        <Trash2 className="h-3 w-3" />
                                    </Button>
                                </div>
                            </div>
                        ))}
                    </div>
                ) : (
                    <div className="flex min-h-48 items-center justify-center text-center text-muted-foreground">
                        <div>
                            <FileVideo className="mx-auto mb-2 h-12 w-12" />
                            <p>No files uploaded yet</p>
                        </div>
                    </div>
                )}
            </CardContent>
        </Card>
    );
}