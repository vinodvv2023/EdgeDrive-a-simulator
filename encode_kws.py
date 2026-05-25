import sentencepiece as spm
sp = spm.SentencePieceProcessor()
sp.load('external/sherpa-onnx-kws-model/bpe.model')

def encode(word):
    pieces = sp.encode_as_pieces(word.upper())
    # sherpa-onnx expects ' ' (U+2581) to be represented as standard space, or they use ▁?
    # Actually, sherpa-onnx gigaspeech keywords.txt uses U+2591 ░ or U+2581   ?
    # Let's just output raw pieces joined by spaces, and replace sentencepiece's " " with the character used in the original keywords.txt
    
    # Sentencepiece uses U+2581 ' '
    return " ".join(pieces)

with open('external/sherpa-onnx-kws-model/keywords.txt', 'w', encoding='utf-8') as f:
    # Adding phonetic variations dramatically improves recognition for custom words!
    f.write(encode("Go Visa") + "\n")
    f.write(encode("Covesa") + "\n")
    f.write(encode("Co vesa") + "\n")
    f.write(encode("Ko vesa") + "\n")
    f.write(encode("Cuh vesa") + "\n")
    f.write(encode("Co-veh-sa") + "\n")
    f.write(encode("Co-beh-sa") + "\n")
    f.write(encode("koh-veh-sa") + "\n")
    
    f.write(encode("Alexa") + "\n")
    f.write(encode("stop playing") + "\n")
    f.write(encode("stop music") + "\n")
    f.write(encode("stop") + "\n")
