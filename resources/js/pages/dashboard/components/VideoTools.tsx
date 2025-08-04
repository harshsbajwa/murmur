import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Button } from '@/components/ui/button';
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '@/components/ui/select';
import { Separator } from '@/components/ui/separator';
import { Film, Settings, FileText, Languages, Download, Copy, CheckCircle, Loader2 } from 'lucide-react';
import { useState } from 'react';
import { TranscriptionResult } from '@/types';
import { Progress } from '@/components/ui/progress';

const supportedFormats = [
    { value: 'mp4', label: 'MP4', description: 'Most compatible' },
    { value: 'webm', label: 'WebM', description: 'Web optimized' },
    { value: 'avi', label: 'AVI', description: 'High quality' },
    { value: 'mov', label: 'MOV', description: 'Apple format' },
];

const languages = [
    { code: 'en', name: 'English' },
    { code: 'zh', name: 'Chinese' },
    { code: 'de', name: 'German' },
    { code: 'es', name: 'Spanish' },
    { code: 'ru', name: 'Russian' },
    { code: 'ko', name: 'Korean' },
    { code: 'fr', name: 'French' },
    { code: 'ja', name: 'Japanese' },
    { code: 'pt', name: 'Portuguese' },
    { code: 'tr', name: 'Turkish' },
    { code: 'pl', name: 'Polish' },
    { code: 'ca', name: 'Catalan' },
    { code: 'nl', name: 'Dutch' },
    { code: 'ar', name: 'Arabic' },
    { code: 'sv', name: 'Swedish' },
    { code: 'it', name: 'Italian' },
    { code: 'id', name: 'Indonesian' },
    { code: 'hi', name: 'Hindi' },
    { code: 'fi', name: 'Finnish' },
    { code: 'vi', name: 'Vietnamese' },
    { code: 'uk', name: 'Ukrainian' },
    { code: 'el', name: 'Greek' },
    { code: 'ms', name: 'Malay' },
    { code: 'cs', name: 'Czech' },
    { code: 'ro', name: 'Romanian' },
    { code: 'da', name: 'Danish' },
    { code: 'hu', name: 'Hungarian' },
    { code: 'ta', name: 'Tamil' },
    { code: 'no', name: 'Norwegian' },
    { code: 'th', name: 'Thai' },
    { code: 'ur', name: 'Urdu' },
    { code: 'hr', name: 'Croatian' },
    { code: 'bg', name: 'Bulgarian' },
    { code: 'la', name: 'Latin' },
    { code: 'mi', name: 'Maori' },
    { code: 'ml', name: 'Malayalam' },
    { code: 'cy', name: 'Welsh' },
    { code: 'sk', name: 'Slovak' },
    { code: 'te', name: 'Telugu' },
    { code: 'fa', name: 'Persian' },
    { code: 'bn', name: 'Bengali' },
    { code: 'sr', name: 'Serbian' },
    { code: 'sl', name: 'Slovenian' },
    { code: 'kn', name: 'Kannada' },
    { code: 'mk', name: 'Macedonian' },
    { code: 'br', name: 'Breton' },
    { code: 'eu', name: 'Basque' },
    { code: 'is', name: 'Icelandic' },
    { code: 'hy', name: 'Armenian' },
    { code: 'ne', name: 'Nepali' },
    { code: 'mn', name: 'Mongolian' },
    { code: 'bs', name: 'Bosnian' },
    { code: 'kk', name: 'Kazakh' },
    { code: 'sq', name: 'Albanian' },
    { code: 'sw', name: 'Swahili' },
    { code: 'gl', name: 'Galician' },
    { code: 'mr', name: 'Marathi' },
    { code: 'pa', name: 'Punjabi' },
    { code: 'si', name: 'Sinhala' },
    { code: 'km', name: 'Khmer' },
    { code: 'sn', name: 'Shona' },
    { code: 'yo', name: 'Yoruba' },
    { code: 'so', name: 'Somali' },
    { code: 'af', name: 'Afrikaans' },
    { code: 'oc', name: 'Occitan' },
    { code: 'ka', name: 'Georgian' },
    { code: 'be', name: 'Belarusian' },
    { code: 'tg', name: 'Tajik' },
    { code: 'sd', name: 'Sindhi' },
    { code: 'gu', name: 'Gujarati' },
    { code: 'am', name: 'Amharic' },
    { code: 'yi', name: 'Yiddish' },
    { code: 'lo', name: 'Lao' },
    { code: 'uz', name: 'Uzbek' },
    { code: 'fo', name: 'Faroese' },
    { code: 'ht', name: 'Haitian' },
    { code: 'ps', name: 'Pashto' },
    { code: 'tk', name: 'Turkmen' },
    { code: 'nn', name: 'Nynorsk' },
    { code: 'mt', name: 'Maltese' },
    { code: 'sa', name: 'Sanskrit' },
    { code: 'my', name: 'Burmese' },
    { code: 'bo', name: 'Tibetan' },
    { code: 'tl', name: 'Tagalog' },
    { code: 'mg', name: 'Malagasy' },
    { code: 'as', name: 'Assamese' },
    { code: 'tt', name: 'Tatar' },
    { code: 'haw', name: 'Hawaiian' },
    { code: 'ln', name: 'Lingala' },
    { code: 'ha', name: 'Hausa' },
    { code: 'ba', name: 'Bashkir' },
    { code: 'jw', name: 'Javanese' },
    { code: 'su', name: 'Sundanese' }
];

interface VideoToolsProps {
    onConvert: (format: string) => void;
    onTranscribe: (language: string) => void;
    isReady: boolean;
    currentFileName?: string;
    transcriptionResult?: TranscriptionResult;
    isTranscribing?: boolean;
    conversionProgress?: number;
}

export function VideoTools({ 
    onConvert, 
    onTranscribe, 
    isReady, 
    currentFileName,
    transcriptionResult,
    isTranscribing = false,
    conversionProgress,
}: VideoToolsProps) {
    const [outputFormat, setOutputFormat] = useState('mp4');
    const [transcriptionLanguage, setTranscriptionLanguage] = useState('en');
    const [copied, setCopied] = useState(false);

    const generateVTTContent = (result: TranscriptionResult): string => {
        let vttContent = 'WEBVTT\n\n';
        
        if (result.chunks && result.chunks.length > 0) {
            result.chunks.forEach((chunk, index) => {
                const startTime = formatVTTTime(chunk.timestamp[0]);
                const endTime = formatVTTTime(chunk.timestamp[1]);
                vttContent += `${index + 1}\n`;
                vttContent += `${startTime} --> ${endTime}\n`;
                vttContent += `${chunk.text.trim()}\n\n`;
            });
        } else {
            // If no chunks, create a single subtitle for the whole text
            vttContent += '1\n';
            vttContent += '00:00:00.000 --> 00:10:00.000\n';
            vttContent += `${result.text}\n\n`;
        }
        
        return vttContent;
    };

    const formatVTTTime = (seconds: number): string => {
        const hours = Math.floor(seconds / 3600);
        const minutes = Math.floor((seconds % 3600) / 60);
        const secs = Math.floor(seconds % 60);
        const ms = Math.floor((seconds % 1) * 1000);
        
        return `${hours.toString().padStart(2, '0')}:${minutes.toString().padStart(2, '0')}:${secs.toString().padStart(2, '0')}.${ms.toString().padStart(3, '0')}`;
    };

    const handleDownloadSubtitles = () => {
        if (!transcriptionResult) return;
        
        const vttContent = generateVTTContent(transcriptionResult);
        const blob = new Blob([vttContent], { type: 'text/vtt' });
        const url = URL.createObjectURL(blob);
        
        const a = document.createElement('a');
        a.href = url;
        a.download = `${currentFileName?.split('.')[0] || 'subtitles'}.vtt`;
        document.body.appendChild(a);
        a.click();
        document.body.removeChild(a);
        URL.revokeObjectURL(url);
    };

    const handleCopyToClipboard = async () => {
        if (!transcriptionResult) return;
        
        try {
            await navigator.clipboard.writeText(transcriptionResult.text);
        } catch (err) {
            console.error('Failed to copy text to clipboard:', err);
        }
    };

    const handleCopyClick = () => {
        handleCopyToClipboard();
        setCopied(true);
        setTimeout(() => setCopied(false), 2000);
    };

    return (
        <Card>
            <CardHeader>
                <CardTitle className="flex items-center gap-2">
                    <Settings className="h-5 w-5" />
                    Video Tools
                </CardTitle>
            </CardHeader>
            <Separator />
            <CardContent className="space-y-4">
                <div className="space-y-3">
                    <h4 className="flex items-center gap-2 text-sm font-medium">
                        <Film className="h-4 w-4" /> Conversion
                    </h4>
                    <div>
                        <Select value={outputFormat} onValueChange={setOutputFormat}>
                            <SelectTrigger className="h-10">
                                <SelectValue
                                    placeholder="Select format"
                                    className="text-left"
                                >
                                    {supportedFormats.find(f => f.value === outputFormat)?.label}
                                </SelectValue>
                            </SelectTrigger>
                            <SelectContent>
                                {supportedFormats.map((format) => (
                                    <SelectItem key={format.value} value={format.value} className="py-2">
                                        <div className="flex flex-col">
                                            <span className="font-medium">{format.label}</span>
                                            <span className="text-xs text-gray-500">{format.description}</span>
                                        </div>
                                    </SelectItem>
                                ))}
                            </SelectContent>
                        </Select>
                    </div>
                    <Button 
                        onClick={() => onConvert(outputFormat)} 
                        disabled={!isReady || isTranscribing} 
                        className="w-full"                        
                    >
                        Convert to {outputFormat.toUpperCase()}
                    </Button>
                </div>

                <div className="space-y-3">
                    <h4 className="flex items-center gap-2 text-sm font-medium">
                        <FileText className="h-4 w-4" /> Transcription
                    </h4>
                    <div>
                        <Select value={transcriptionLanguage} onValueChange={setTranscriptionLanguage}>
                            <SelectTrigger className="h-10" id="transcription-language">
                                <SelectValue>
                                    {languages.find(l => l.code === transcriptionLanguage)?.name}
                                </SelectValue>
                            </SelectTrigger>
                            <SelectContent>
                                {languages.map((language) => (
                                    <SelectItem key={language.code} value={language.code}>
                                        <div className="flex items-center gap-2">
                                            <span className="font-mono text-xs px-1.5 py-0.5 rounded bg-gray-100 text-black">
                                                {language.code}
                                            </span>
                                            <span>{language.name}</span>
                                        </div>
                                    </SelectItem>
                                ))}
                            </SelectContent>
                        </Select>
                    </div>
                    <Button 
                        onClick={() => onTranscribe(transcriptionLanguage)} 
                        disabled={!isReady || isTranscribing} 
                        className="w-full" 
                        variant="outline"
                    >                        
                        {isTranscribing ? (
                            <>
                                <Loader2 className="mr-2 h-4 w-4 animate-spin" />
                                Transcribing...
                            </>
                        ) : (
                            'Transcribe'
                        )}
                    </Button>
                    {transcriptionResult && (
                        <div className="space-y-2">
                            <div className="flex items-center gap-2 text-sm text-green-600">
                                <CheckCircle className="h-4 w-4" />
                                <span>Transcription completed</span>
                            </div>
                            <div className="flex gap-2">
                                <Button
                                    onClick={handleDownloadSubtitles}
                                    size="sm"
                                    variant="outline"
                                    className="flex items-center gap-2"
                                >
                                    <Download className="h-4 w-4" />
                                    Download VTT
                                </Button>
                                <Button
                                    onClick={handleCopyClick}
                                    size="sm"
                                    variant="outline"
                                    className="flex items-center gap-2"
                                >
                                    {copied ? (
                                    <>
                                        <span>Copied!</span>
                                    </>
                                    ) : (
                                    <>
                                        <Copy className="h-4 w-4" />
                                        Copy Text
                                    </>
                                    )}
                                </Button>
                            </div>
                        </div>
                    )}
                </div>
            </CardContent>
        </Card>
    );
}